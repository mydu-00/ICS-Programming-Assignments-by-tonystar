#include "utils/ftrace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>

#define MAX_FUNC 1024
typedef struct {
  uint32_t addr, size;
  char name[64];
} FuncSym;
static FuncSym func_syms[MAX_FUNC];
static int func_cnt = 0;
static int ftrace_depth = 0;

static const char *addr_to_func(uint32_t addr, uint32_t *func_addr) {
  for (int i = 0; i < func_cnt; i++) {
    uint32_t start = func_syms[i].addr;
    uint32_t end = start + func_syms[i].size;
    if (addr >= start && addr < end) {
      if (func_addr) *func_addr = start;
      return func_syms[i].name;
    }
  }
  if (func_addr) *func_addr = 0;
  return "???";
}

void ftrace_init(const char *elf_file) {
  FILE *fp = fopen(elf_file, "rb");
  if (!fp) { printf("ftrace: cannot open ELF file %s\n", elf_file); return; }
  Elf32_Ehdr ehdr;
  Elf32_Shdr shdrs[64];

  // 读取 ELF 头和节头
  if (fread(&ehdr, 1, sizeof(ehdr), fp) != sizeof(ehdr)) {
    printf("ftrace: failed to read ELF header\n"); fclose(fp); return;
  }
  if (fread(shdrs, sizeof(Elf32_Shdr), ehdr.e_shnum, fp) != ehdr.e_shnum) {
    printf("ftrace: failed to read section headers\n"); fclose(fp); return;
  }

  // 找到 .symtab 和 .strtab
  Elf32_Shdr *symtab = NULL, *strtab = NULL;
  for (int i = 0; i < ehdr.e_shnum; i++) {
    if (shdrs[i].sh_type == SHT_SYMTAB) symtab = &shdrs[i];
    if (shdrs[i].sh_type == SHT_STRTAB && i != ehdr.e_shstrndx) strtab = &shdrs[i];
  }
  if (!symtab || !strtab) { printf("ftrace: no symtab/strtab\n"); fclose(fp); return; }

  // 读取字符串表
  char *strtab_data = malloc(strtab->sh_size);
  fseek(fp, strtab->sh_offset, SEEK_SET);
  if (fread(strtab_data, 1, strtab->sh_size, fp) != strtab->sh_size) {
    printf("ftrace: failed to read strtab\n"); free(strtab_data); fclose(fp); return;
  }

  // 读取符号表
  int nsyms = symtab->sh_size / symtab->sh_entsize;
  Elf32_Sym *syms = malloc(symtab->sh_size);
  fseek(fp, symtab->sh_offset, SEEK_SET);
  if (fread(syms, symtab->sh_entsize, nsyms, fp) != nsyms) {
    printf("ftrace: failed to read symtab\n"); free(strtab_data); free(syms); fclose(fp); return;
  }

  // 记录所有 FUNC
  func_cnt = 0;
  for (int i = 0; i < nsyms && func_cnt < MAX_FUNC; i++) {
    if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_size > 0) {
      func_syms[func_cnt].addr = syms[i].st_value;
      func_syms[func_cnt].size = syms[i].st_size;
      strncpy(func_syms[func_cnt].name, strtab_data + syms[i].st_name, 63);
      func_syms[func_cnt].name[63] = 0;
      func_cnt++;
    }
  }
  free(strtab_data);
  free(syms);
  fclose(fp);
}

void ftrace_call(uint32_t pc, uint32_t target) {
  uint32_t fa;
  const char *fn = addr_to_func(target, &fa);
  printf("%*s0x%08x: call [%s@0x%08x]\n", ftrace_depth * 2, "", pc, fn, fa);
  ftrace_depth++;
}

void ftrace_ret(uint32_t pc) {
  if (ftrace_depth > 0) ftrace_depth--;
  uint32_t fa;
  const char *fn = addr_to_func(pc, &fa);
  printf("%*s0x%08x: ret  [%s]\n", ftrace_depth * 2, "", pc, fn);
}