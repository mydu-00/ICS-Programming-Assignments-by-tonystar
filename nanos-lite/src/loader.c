#include <proc.h>
#include <elf.h>
#include <common.h>
#include <fs.h>

#ifdef HAS_VME
#include <arch/riscv.h>
#ifndef PTE_V
typedef uintptr_t PTE;
#define PTE_V 0x001
#define PTE_R 0x002
#define PTE_W 0x004
#define PTE_X 0x008
#define PTE_U 0x010
#define PTE_G 0x020
#define PTE_A 0x040
#define PTE_D 0x080
#endif
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
      AddrSpace *as = &pcb->as;

      // 以页为单位映射并直接加载文件内容
      uintptr_t file_off   = ph.p_offset;
      uintptr_t cur_va     = va_start;
      size_t    remain_file = ph.p_filesz;
      size_t    remain_mem  = ph.p_memsz;

      for (uintptr_t va = va_page; va < va_end; va += PGSIZE) {
        void *pa = new_page(1);
        memset(pa, 0, PGSIZE);
        map(as, (void *)va, pa, 0);

        // 本页的起始和结束虚拟地址
        uintptr_t page_va_start = va;
        uintptr_t page_va_end   = va + PGSIZE;

        // 这一页在 segment 中实际覆盖的范围
        uintptr_t seg_page_start = (cur_va > page_va_start) ? cur_va : page_va_start;
        uintptr_t seg_page_end   = (page_va_end < va_start + ph.p_memsz)
                                   ? page_va_end : (va_start + ph.p_memsz);

        size_t page_bytes = 0;
        if (seg_page_end > seg_page_start) {
          page_bytes = seg_page_end - seg_page_start;
        }

        // 如果这一页有文件内容部分，就从文件读入到对应的物理页偏移
        if (page_bytes > 0 && remain_file > 0) {
          size_t file_bytes = page_bytes;
          if (file_bytes > remain_file) file_bytes = remain_file;

          // 这一页内的偏移
          size_t page_off = seg_page_start - page_va_start;

          fs_lseek(fd, file_off, SEEK_SET);
          size_t nread = fs_read(fd, (uint8_t *)pa + page_off, file_bytes);
          assert(nread == file_bytes);

          file_off    += file_bytes;
          cur_va      += file_bytes;
          remain_file -= file_bytes;
          remain_mem  -= file_bytes;
        } else {
          // 全在 BSS 区或 segment 末尾，pa 已经 memset 0 了
          if (page_bytes > 0) {
            remain_mem -= page_bytes;
            cur_va     += page_bytes;
          }
        }
      }

      // 到这里为止，文件区已读入，各页剩余部分已清零，memsz 已覆盖完
#else
      // 原来的非 VME 路径保持不变
      void *seg_dst = (void *)(uintptr_t)ph.p_vaddr;
      if (ph.p_filesz > 0) {
        fs_lseek(fd, ph.p_offset, SEEK_SET);
        size_t n2 = fs_read(fd, seg_dst, (size_t)ph.p_filesz);
        assert(n2 == (size_t)ph.p_filesz);
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

