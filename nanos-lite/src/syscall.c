#include <common.h>
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

  if (id == 0) {
    printf("[diag] gpr[15](a5)=%x gpr[17](a7)=%x\n",
           (unsigned)c->gpr[15],
           (unsigned)((NR_REGS > 17) ? c->gpr[17] : 0));
  }

  switch (id) {
    case SYS_yield:
      // 关键修复：不要再次调用 yield() 以避免嵌套 ecall 和双重 mepc +=4
      // 如果有调度器，可在这里调用 schedule();
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
