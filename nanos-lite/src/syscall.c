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
  uintptr_t id = c->GPR1;
  uintptr_t arg0 = c->GPR2;
  uintptr_t arg1 = c->GPR3;
  uintptr_t arg2 = c->GPR4;

  printf("[do_syscall] id=%u raw(a5)=0x%x a0=%u a1=%u a2=%u\n",
         (unsigned)id, (unsigned)c->GPR1,
         (unsigned)arg0, (unsigned)arg1, (unsigned)arg2);

  if (id == 0) {
    printf("[diag] gpr[15](a5)=%x gpr[17](a7)=%x (both shown to检测混配)\n",
           (unsigned)c->gpr[15],
           (unsigned)((NR_REGS > 17) ? c->gpr[17] : 0));
  }

  switch (id) {
    case SYS_yield:
      yield();
      c->GPRx = 0;
      break;
    case SYS_exit:
      halt((int)arg0);
      c->GPRx = 0;
      break;
    default:
      panic("Unhandled syscall ID = %u", (unsigned)id);
  }
}
