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
  uintptr_t id = c->GPR1;      // a5 in RV32E
  uintptr_t arg0 = c->GPR2;    // a0
  uintptr_t arg1 = c->GPR3;    // a1
  uintptr_t arg2 = c->GPR4;    // a2
  printf("[do_syscall] id=%lu raw(a5)=0x%lx a0=%lu a1=%lu a2=%lu\n",
         (unsigned long)id, (unsigned long)c->GPR1,
         (unsigned long)arg0, (unsigned long)arg1, (unsigned long)arg2);

  switch (id) {
    case SYS_yield:
      yield();
      c->GPRx = 0;
      break;
    case SYS_exit:
      halt((int)arg0);   // 约定: exit(status) 放在 a0
      c->GPRx = 0;
      break;
    default:
      panic("Unhandled syscall ID = %lu", (unsigned long)id);
  }
}
