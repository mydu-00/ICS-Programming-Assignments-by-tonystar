#ifndef ARCH_H__
#define ARCH_H__

#ifdef __riscv_e
#define NR_REGS 16
#else
#define NR_REGS 32
#endif

struct Context {
  uintptr_t gpr[NR_REGS];   // x1, x3, x4, ..., x31（x0位置用于地址空间信息）
  void *pdir;               // 地址空间信息（与gpr[0]共用空间）
  uintptr_t mcause;         // 异常号
  uintptr_t mstatus;        // 处理器状态
  uintptr_t mepc;           // 触发异常时的PC
};

#ifdef __riscv_e
#define GPR1 gpr[15] // a5
#else
#define GPR1 gpr[17] // a7
#endif

#define GPR2 gpr[0]
#define GPR3 gpr[0]
#define GPR4 gpr[0]
#define GPRx gpr[0]

#endif
