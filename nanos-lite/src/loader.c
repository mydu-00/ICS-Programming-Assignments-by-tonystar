#include <proc.h>
#include <elf.h>
#include <common.h>
#include <fs.h>
#include <nemu.h>
#ifdef HAS_VME
#endif

#ifdef HAS_VME
static inline void *va2pa(AddrSpace *as, uintptr_t va) {
  PTE *root = (PTE *)as->ptr;
  uint32_t vpn1 = (va >> 22) & 0x3ff;
  uint32_t vpn0 = (va >> 12) & 0x3ff;

  PTE pte1 = root[vpn1];
  assert(pte1 & PTE_V);
  PTE *pt = (PTE *)(((uintptr_t)pte1 >> 10) << 12);

  PTE pte0 = pt[vpn0];
  assert(pte0 & PTE_V);

  uintptr_t pa = ((uintptr_t)(pte0 >> 10) << 12) | (va & 0xfff);
  return (void *)pa;
}
#endif

#ifdef __LP64__
# define Elf_Ehdr Elf64_Ehdr
# define Elf_Phdr Elf64_Phdr
#else
# define Elf_Ehdr Elf32_Ehdr
# define Elf_Phdr Elf32_Phdr
#endif

#if defined(__ISA_AM_NATIVE__)
# define EXPECT_TYPE EM_X86_64
#elif defined(__ISA_X86__)
# ifdef __LP64__
#  define EXPECT_TYPE EM_X86_64
# else
#  define EXPECT_TYPE EM_386
# endif
#elif defined(__ISA_MIPS32__)
# define EXPECT_TYPE EM_MIPS
#elif defined(__riscv)
# define EXPECT_TYPE EM_RISCV
#else
# error "Unsupported ISA for loader ELF check"
#endif

// 从 PCB 里取地址空间描述符
static inline AddrSpace *pcb_as(PCB *pcb) {
  return &pcb->as;
}

uintptr_t loader(PCB *pcb, const char *filename) {
  Elf_Ehdr ehdr;

  int fd = fs_open(filename, 0, 0);
  assert(fd >= 0);

  fs_lseek(fd, 0, SEEK_SET);
  size_t n = fs_read(fd, &ehdr, sizeof(Elf_Ehdr));
  assert(n == sizeof(Elf_Ehdr));

  assert(ehdr.e_ident[EI_MAG0] == ELFMAG0 &&
         ehdr.e_ident[EI_MAG1] == ELFMAG1 &&
         ehdr.e_ident[EI_MAG2] == ELFMAG2 &&
         ehdr.e_ident[EI_MAG3] == ELFMAG3);
  assert(ehdr.e_machine == EXPECT_TYPE);

  AddrSpace *as = pcb_as(pcb);

#ifdef HAS_VME
  // 为该进程创建地址空间（含内核映射）
  protect(as);
#endif

  for (int i = 0; i < ehdr.e_phnum; i++) {
    Elf_Phdr ph;
    size_t ph_off = (size_t)ehdr.e_phoff + i * (size_t)ehdr.e_phentsize;

    fs_lseek(fd, ph_off, SEEK_SET);
    n = fs_read(fd, &ph, sizeof(Elf_Phdr));
    assert(n == sizeof(Elf_Phdr));

    if (ph.p_type == PT_LOAD && ph.p_memsz > 0) {
      uintptr_t va_start = (uintptr_t)ph.p_vaddr;
      uintptr_t va_end   = va_start + ph.p_memsz;
      uintptr_t va_page  = ROUNDDOWN(va_start, PGSIZE);

#ifdef HAS_VME
      // 为段涉及的每一页分配物理页并映射
      for (uintptr_t va = va_page; va < va_end; va += PGSIZE) {
        void *pa = new_page(1);
        memset(pa, 0, PGSIZE);
        map(as, (void *)va, pa, 0);  // AM native 里忽略 prot，默认 R/W/X
      }

      fs_lseek(fd, ph.p_offset, SEEK_SET);
      size_t copied = 0;
      while (copied < ph.p_filesz) {
        uintptr_t va = va_start + copied;
        size_t page_off = va & (PGSIZE - 1);
        size_t remain = ph.p_filesz - copied;
        size_t chunk = (PGSIZE - page_off < remain) ? (PGSIZE - page_off) : remain;

        void *pa = va2pa(as, va);
        size_t nread = fs_read(fd, pa, chunk);
        assert(nread == chunk);

        copied += chunk;
      }
#else
      // 未开启 VME：老办法，直接写 vaddr
      void *seg_dst = (void *)(uintptr_t)ph.p_vaddr;
      if (ph.p_filesz > 0) {
        fs_lseek(fd, ph.p_offset, SEEK_SET);
        n = fs_read(fd, seg_dst, (size_t)ph.p_filesz);
        assert(n == (size_t)ph.p_filesz);
      }
      if (ph.p_memsz > ph.p_filesz) {
        memset((char *)seg_dst + ph.p_filesz, 0, (size_t)(ph.p_memsz - ph.p_filesz));
      }
#endif

      Log("Loaded segment: off=0x%x vaddr=%p filesz=%u memsz=%u",
          (unsigned)ph.p_offset, (void *)ph.p_vaddr,
          (unsigned)ph.p_filesz, (unsigned)ph.p_memsz);
    }
  }

  fs_close(fd);
  return (uintptr_t)ehdr.e_entry;
}

void naive_uload(PCB *pcb, const char *filename) {
  uintptr_t entry = loader(pcb, filename);
  Log("Jump to entry = %p", entry);
  ((void(*)())entry) ();
}

