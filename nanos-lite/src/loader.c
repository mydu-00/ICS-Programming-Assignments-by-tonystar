#include <proc.h>
#include <elf.h>
#include <common.h>
#include <fs.h>

#define CONFIG_MBASE 0x80000000u

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

static inline uintptr_t phys_addr(uintptr_t va) {
  // 只有当编译出的 ELF 段地址还没带物理基址时再补
  if (va < CONFIG_MBASE) return va + CONFIG_MBASE;
  return va;
}

static uintptr_t loader(PCB *pcb, const char *filename) {
  (void)pcb;
  Elf_Ehdr eh;
  int fd = fs_open(filename, 0, 0);
  fs_lseek(fd, 0, SEEK_SET);
  assert(fs_read(fd, &eh, sizeof(eh)) == sizeof(eh));

  assert(eh.e_ident[EI_MAG0] == ELFMAG0 &&
         eh.e_ident[EI_MAG1] == ELFMAG1 &&
         eh.e_ident[EI_MAG2] == ELFMAG2 &&
         eh.e_ident[EI_MAG3] == ELFMAG3);
  assert(eh.e_machine == EXPECT_TYPE);

  for (int i = 0; i < eh.e_phnum; i++) {
    Elf_Phdr ph;
    size_t off = eh.e_phoff + i * eh.e_phentsize;
    fs_lseek(fd, off, SEEK_SET);
    assert(fs_read(fd, &ph, sizeof(ph)) == sizeof(ph));
    if (ph.p_type != PT_LOAD) continue;

    uintptr_t dest = phys_addr(ph.p_vaddr);
    if (ph.p_filesz) {
      fs_lseek(fd, ph.p_offset, SEEK_SET);
      assert(fs_read(fd, (void *)dest, ph.p_filesz) == ph.p_filesz);
    }
    if (ph.p_memsz > ph.p_filesz) {
      memset((void *)(dest + ph.p_filesz), 0, ph.p_memsz - ph.p_filesz);
    }
    Log("SEG load: p_vaddr=0x%08x phys=0x%08x file=0x%x mem=0x%x",
        (unsigned)ph.p_vaddr, (unsigned)dest,
        (unsigned)ph.p_filesz, (unsigned)ph.p_memsz);
  }
  fs_close(fd);
  uintptr_t entry = phys_addr(eh.e_entry);
  Log("ELF entry v=0x%08x phys=0x%08x", (unsigned)eh.e_entry, (unsigned)entry);
  return entry;
}

void naive_uload(PCB *pcb, const char *filename) {
  uintptr_t entry = loader(pcb, filename);
  ((void(*)())entry)();
}

