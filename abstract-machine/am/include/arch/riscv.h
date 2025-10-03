#ifndef ARCH_H__
#define ARCH_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __riscv_e
#define NR_REGS 16
#else
#define NR_REGS 32
#endif

struct Context {
  union {
    struct {
      void *pdir;                 // 占用原 gpr[0] 槽 (x0 未被保存真实值)
      uintptr_t gpr_rest[NR_REGS - 1]; // x1..x31
    };
    uintptr_t gpr[NR_REGS];       // 按寄存器号索引: gpr[0]..gpr[NR_REGS-1]
  };
  uintptr_t mcause;               // offset = NR_REGS * sizeof(uintptr_t)
  uintptr_t mstatus;
  uintptr_t mepc;
};

_Static_assert(offsetof(struct Context, mcause) == sizeof(uintptr_t) * NR_REGS,
               "Context layout mismatch with trap.S");

#ifdef __riscv_e
#define GPR1 gpr[15]   // a5
#else
#define GPR1 gpr[17]   // a7
#endif
#define GPR2 gpr[0]
#define GPR3 gpr[0]
#define GPR4 gpr[0]
#define GPRx gpr[0]

#endif
