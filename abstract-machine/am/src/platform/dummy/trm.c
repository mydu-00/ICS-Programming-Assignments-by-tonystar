#include <am.h>

Area heap = RANGE(NULL, NULL);

void putch(char ch) {
}

void halt(int code) {
  asm volatile("ebreak");
}
