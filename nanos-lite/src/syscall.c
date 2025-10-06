#include <common.h>
#include "syscall.h"

#ifndef SYS_yield
#define SYS_yield 1
#endif

#ifndef SYS_exit
#define SYS_exit 60
#endif

/* perform syscall handling; set return value via c->GPRx */
void do_syscall(Context *c) {
  // debug dump: print relevant registers once per syscall
  printf("DO_SYSCALL: mcause=0x%08lx mepc=0x%08lx\n", c->mcause, c->mepc);
  for (int i = 10; i <= 17; i++) {
    printf(" gpr[%02d]=0x%08lx", i, c->gpr[i]);
    if (i == 13 || i == 17) printf("\n");
  }

  uintptr_t a[4];
  a[0] = c->GPR1;
  a[1] = c->GPR2;
  a[2] = c->GPR3;
  a[3] = c->GPR4;
  printf(" extracted: num=%lu a0=%lu a1=%lu a2=%lu\n", (unsigned long)a[0], (unsigned long)a[1], (unsigned long)a[2], (unsigned long)a[3]);

  switch (a[0]) {
    case SYS_yield:
      /* For SYS_yield, simply invoke yield (CTE) and return 0 */
      yield();
      c->GPRx = 0;
      break;

    case SYS_exit:
      /* SYS_exit(status): directly halt the machine with given status */
      halt((int)a[1]);
      /* not reached; keep for clarity */
      c->GPRx = 0;
      break;

    default:
      panic("Unhandled syscall ID = %d", (int)a[0]);
  }
}
