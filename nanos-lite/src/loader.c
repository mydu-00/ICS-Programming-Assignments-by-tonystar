#include <proc.h>
#include <elf.h>
#include <common.h>
#include <fs.h>   // 这里不需要 CONFIG_MBASE

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

uintptr_t loader(PCB *pcb, const char *filename) {
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

  /* iterate program headers and load PT_LOAD segments */
  for (int i = 0; i < ehdr.e_phnum; i++) {
    Elf_Phdr ph;
    size_t ph_off = (size_t)ehdr.e_phoff + i * (size_t)ehdr.e_phentsize;

    fs_lseek(fd, ph_off, SEEK_SET);
    n = fs_read(fd, &ph, sizeof(Elf_Phdr));
    assert(n == sizeof(Elf_Phdr));

    if (ph.p_type == PT_LOAD) {
      void *seg_dst = (void *)(uintptr_t)ph.p_vaddr;  // 直接用 p_vaddr

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
    }
  }

  fs_close(fd);
  return (uintptr_t)ehdr.e_entry;  // 同样不要再偏移
}

void naive_uload(PCB *pcb, const char *filename) {
  uintptr_t entry = loader(pcb, filename);
  Log("Jump to entry = %p", entry);
  ((void(*)())entry) ();
}

