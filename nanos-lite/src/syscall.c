#include <common.h>
#include "/home/tony/codingproj/ics2025/abstract-machine/am/include/am.h"

#include "syscall.h"

/* perform syscall handling; set return value via c->GPRx */
void do_syscall(Context *c) {
  uintptr_t id   = c->GPR1;
  uintptr_t arg0 = c->GPR2;
  uintptr_t arg1 = c->GPR3;
  uintptr_t arg2 = c->GPR4;

  printf("[do_syscall] id=%u raw(GPR1)=0x%x a0=%u a1=%u a2=%u\n",
         (unsigned)id, (unsigned)c->GPR1,
         (unsigned)arg0, (unsigned)arg1, (unsigned)arg2);

  if (id == SYS_exit) {
    printf("[diag] exit status=%u\n", (unsigned)arg0);
  }

  switch (id) {
    case SYS_yield:
      // yield();          
      c->GPRx = 0;
      break;

    case SYS_exit:
      halt((int)arg0); 
      break;

    default:
      panic("Unhandled syscall ID = %u", (unsigned)id);
  }
}
