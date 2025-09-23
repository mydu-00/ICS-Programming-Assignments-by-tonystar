#ifndef __FTRACE_H__
#define __FTRACE_H__

#include <stdint.h>

void ftrace_init(const char *elf_file);
void ftrace_call(uint32_t pc, uint32_t target);
void ftrace_ret(uint32_t pc);

#endif