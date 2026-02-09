#include <proc.h>
#include <elf.h>
#include <common.h>
#include <fs.h>

// 声明 new_page, 因为没有头文件包含它
extern void* new_page(size_t nr_page);

#ifdef __LP64__
# define Elf_Ehdr Elf64_Ehdr
# define Elf_Phdr Elf64_Phdr
#else
# define Elf_Ehdr Elf32_Ehdr
# define Elf_Phdr Elf32_Phdr
#endif

/* expected ELF e_machine for this build */
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

static uintptr_t loader(PCB *pcb, const char *filename) {
  Elf_Ehdr ehdr;

  int fd = fs_open(filename, 0, 0);
  assert(fd >= 0);

  /* read ELF header from beginning of the file */
  fs_lseek(fd, 0, SEEK_SET);
  size_t n = fs_read(fd, &ehdr, sizeof(Elf_Ehdr));
  assert(n == sizeof(Elf_Ehdr));

  /* basic ELF magic check */
  assert(ehdr.e_ident[EI_MAG0] == ELFMAG0 &&
         ehdr.e_ident[EI_MAG1] == ELFMAG1 &&
         ehdr.e_ident[EI_MAG2] == ELFMAG2 &&
         ehdr.e_ident[EI_MAG3] == ELFMAG3);

  /* ensure the ELF is for the current ISA */
  assert(ehdr.e_machine == EXPECT_TYPE);

#ifdef HAS_VME
  // 初始化虚拟地址空间
  protect(&pcb->as);
#endif

  /* iterate program headers and load PT_LOAD segments */
  for (int i = 0; i < ehdr.e_phnum; i++) {
    Elf_Phdr ph;
    size_t ph_off = (size_t)ehdr.e_phoff + i * (size_t)ehdr.e_phentsize;

    fs_lseek(fd, ph_off, SEEK_SET);
    n = fs_read(fd, &ph, sizeof(Elf_Phdr));
    assert(n == sizeof(Elf_Phdr));

    if (ph.p_type == PT_LOAD) {
      void *seg_dst = (void *)(uintptr_t)ph.p_vaddr;

#ifdef HAS_VME
      // PA3: VME Loading Logic
      uintptr_t vaddr = (uintptr_t)seg_dst;
      uintptr_t mem_sz = ph.p_memsz;
      uintptr_t file_sz = ph.p_filesz;

      uintptr_t page_start_vaddr = ROUNDDOWN(vaddr, PGSIZE);
      uintptr_t page_end_vaddr = ROUNDUP(vaddr + mem_sz, PGSIZE);
      
      fs_lseek(fd, ph.p_offset, SEEK_SET);

      uintptr_t current_vaddr = page_start_vaddr;
      
      while (current_vaddr < page_end_vaddr) {
        // Alloc physical page
        void *paddr = new_page(1);
        
        // Map vaddr -> paddr
        map(&pcb->as, (void *)current_vaddr, paddr, 0);
        
        // Calculate copy range for this page
        // Page range: [current_vaddr, current_vaddr + PGSIZE)
        // Segment range in memory: [vaddr, vaddr + mem_sz)
        // Segment data from file: [vaddr, vaddr + file_sz)
        
        // Clear page first
        memset(paddr, 0, PGSIZE);

        uintptr_t copy_start = (current_vaddr < vaddr) ? vaddr : current_vaddr;
        uintptr_t copy_end = (current_vaddr + PGSIZE > vaddr + file_sz) ? (vaddr + file_sz) : (current_vaddr + PGSIZE);
        
        if (copy_start < copy_end) {
          uintptr_t phys_off = copy_start - current_vaddr; // offset in page
          uintptr_t len = copy_end - copy_start;
          
          // Seek to correct pos in file
          fs_lseek(fd, ph.p_offset + (copy_start - vaddr), SEEK_SET);
          fs_read(fd, (void *)((uintptr_t)paddr + phys_off), len);
        }
        
        current_vaddr += PGSIZE;
      }
#else
      if (ph.p_filesz > 0) {
        fs_lseek(fd, ph.p_offset, SEEK_SET);
        n = fs_read(fd, seg_dst, (size_t)ph.p_filesz);
        assert(n == (size_t)ph.p_filesz);
      }

      if (ph.p_memsz > ph.p_filesz) {
        memset((char *)seg_dst + ph.p_filesz, 0, (size_t)(ph.p_memsz - ph.p_filesz));
      }

      Log("Loaded segment: off=0x%x vaddr=%p filesz=%u memsz=%u",
          (unsigned)ph.p_offset, (void *)ph.p_vaddr, (unsigned)ph.p_filesz, (unsigned)ph.p_memsz);
#endif
    }
  }

  fs_close(fd);
  return (uintptr_t)ehdr.e_entry;  
}

void naive_uload(PCB *pcb, const char *filename) {
  uintptr_t entry = loader(pcb, filename);
  Log("Jump to entry = %p", entry);
#ifdef HAS_VME
  // Switch to the user address space before jumping
  // This is a temporary hack for naive_uload with VME. 
  // Proper implementation should use context switching.
  #ifdef __riscv
    uintptr_t pdir = (uintptr_t)pcb->as.ptr;
    // Sv32 mode bit is 31
    uintptr_t mode = 0x80000000;
    uintptr_t satp = mode | (pdir >> 12);
    asm volatile("csrw satp, %0" : : "r"(satp));
    asm volatile("sfence.vma");
  #endif
#endif
  ((void(*)())entry) ();
}

