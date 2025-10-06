#include <common.h>
#include "syscall.h"

#ifndef SYS_yield
#define SYS_yield 1
#endif

#ifndef SYS_exit
#define SYS_exit 0
#endif

/* perform syscall handling; set return value via c->GPRx */
void do_syscall(Context *c) {
  uintptr_t a[4];
  a[0] = c->GPR1;
  a[1] = c->GPR2;
  a[2] = c->GPR3;
  a[3] = c->GPR4;

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
