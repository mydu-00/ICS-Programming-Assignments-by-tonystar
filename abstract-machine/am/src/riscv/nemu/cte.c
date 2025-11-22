#include <am.h>
#include <arch/riscv.h>
#include <klib.h>

// 这两行是关键：声明 VME 提供的接口
extern void __am_get_cur_as(Context *c);
extern void __am_switch(Context *c);

static Context* (*user_handler)(Event, Context*) = NULL;

Context* __am_irq_handle(Context *c) {
  // 1) 记录当前地址空间指针到 Context（从 satp 读）
  __am_get_cur_as(c);

  if (user_handler) {
    Event ev = {0};
    switch (c->mcause) {
      case 8:
      case 11:
#ifdef __riscv_e
        // printf("[trap] mcause=0x%x a5=0x%x\n",
        //        (unsigned)c->mcause, (unsigned)c->gpr[15]);
        if (c->gpr[15] == (uintptr_t)-1) ev.event = EVENT_YIELD;
#else
        // printf("[trap] mcause=0x%x a7=0x%x\n",
        //        (unsigned)c->mcause, (unsigned)c->gpr[17]);
        if (c->gpr[17] == (uintptr_t)-1) ev.event = EVENT_YIELD;
#endif
        else ev.event = EVENT_SYSCALL;
        break;
      case 0x80000007: ev.event = EVENT_IRQ_TIMER; break;
      case 0x8000000b: ev.event = EVENT_IRQ_IODEV; break;
      default: ev.event = EVENT_ERROR; break;
    }
    if (ev.event == EVENT_YIELD || ev.event == EVENT_SYSCALL) {
      c->mepc = (uintptr_t)(c->mepc + 4);
    }
    c = user_handler(ev, c);
    assert(c != NULL);
  }

  // 2) 切换到被调度进程的地址空间（写 satp）
  __am_switch(c);
  return c;
}

extern void __am_asm_trap(void);

bool cte_init(Context*(*handler)(Event, Context*)) {
  // initialize exception entry
  asm volatile("csrw mtvec, %0" : : "r"(__am_asm_trap));

  // register event handler
  user_handler = handler;

  return true;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  uintptr_t top = (uintptr_t)kstack.end;
  Context *ctx = (Context *)(top - sizeof(Context));
  assert((uintptr_t)ctx >= (uintptr_t)kstack.start);
  memset(ctx, 0, sizeof(Context));

  // 初始为 M-mode, MPP = 11
  ctx->mstatus = (uintptr_t)(3UL << 11);

  // 入口地址
  ctx->mepc = (uintptr_t)entry;

  // 按 RISC-V ABI 传参: a0(x10) = arg
  ctx->GPR2 = (uintptr_t)arg;

  return ctx;
}

void yield() {
#ifdef __riscv_e
  asm volatile("li a5, -1; ecall");
#else
  asm volatile("li a7, -1; ecall");
#endif
}

bool ienabled() {
  return false;
}

void iset(bool enable) {
}
