#include <am.h>

Area heap = RANGE(NULL, NULL);

void putch(char ch) {
}

void halt(int code) {
  const char *msg = "halt called\n";
  for (const char *p = msg; *p; p++) putch(*p);
  asm volatile("ebreak");
  while(1);
}
