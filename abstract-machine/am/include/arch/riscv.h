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
      void *pdir;                        // share the slot of gpr[0]
      uintptr_t gpr_rest[NR_REGS - 1];  // x1..x31 (or up to NR_REGS-1)
    };
    uintptr_t gpr[NR_REGS];            // indexed by register number
  };
  uintptr_t mcause;   // offset = NR_REGS * sizeof(uintptr_t)
  uintptr_t mstatus;
  uintptr_t mepc;
};

_Static_assert(offsetof(struct Context, mcause) == sizeof(uintptr_t) * NR_REGS,
               "Context layout mismatch with trap.S");

/* 强制使用 a7 作为 syscall 号寄存器，忽略 rv32e 分支，避免编译宏不一致 */
#undef GPR1
#define GPR1 gpr[17]   /* a7 */

/* argument registers: a0..a2 = x10..x12 */
#define GPR2 gpr[10]   /* a0 */
#define GPR3 gpr[11]   /* a1 */
#define GPR4 gpr[12]   /* a2 */

/* return value register: a0 (x10) */
#define GPRx gpr[10]   /* a0 */

#endif
