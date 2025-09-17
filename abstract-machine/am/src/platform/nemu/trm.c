#include <am.h>
#include <nemu.h>
#include <stdio.h>

extern char _heap_start;
int main(const char *args);

Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

void putch(char ch) {
  outb(SERIAL_PORT, ch);
}

void halt(int code) {
  const char *msg = "halt called\n";
  for (const char *p = msg; *p; p++) putch(*p);
  asm volatile("ebreak");
  while(1);
}

void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
