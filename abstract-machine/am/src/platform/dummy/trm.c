#include <am.h>

Area heap = RANGE(NULL, NULL);

void putch(char ch) {
}

void halt(int code) {
  printf("halt called\n"); fflush(stdout);
  asm volatile("ebreak");
  while(1);
}
