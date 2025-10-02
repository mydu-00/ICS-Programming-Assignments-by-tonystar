#include <am.h>
#include <arch/riscv.h>
#include <klib.h>
#include <stdio.h>

static Context* (*user_handler)(Event, Context*) = NULL;

Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};
    switch (c->mcause) {
      case 11: // 环境调用（ecall）
#ifdef __riscv_e
        if (c->gpr[15] == (uintptr_t)-1) ev.event = EVENT_YIELD; // a5
#else
        if (c->gpr[17] == (uintptr_t)-1) ev.event = EVENT_YIELD; // a7
#endif
        else ev.event = EVENT_SYSCALL;
        break;
      case 0x80000007: ev.event = EVENT_IRQ_TIMER; break; // timer interrupt
      case 0x8000000b: ev.event = EVENT_IRQ_IODEV; break; // external device interrupt
      default: ev.event = EVENT_ERROR; break;
    }

    c = user_handler(ev, c);
    assert(c != NULL);
  }

  static int irq_count = 0;
  if (irq_count < 5) {
    printf("IRQ #%d: mepc=%lx mcause=%lx mstatus=%lx\n", irq_count, c->mepc, c->mcause, c->mstatus);
    for (int i = 0; i < NR_REGS; i++) {
      printf("gpr[%d]=%lx\n", i, c->gpr[i]);
    }
    printf("pdir=%p\n", c->pdir);
    putch('\n'); // 输出一个换行，确保内容立即显示
    irq_count++;
  }

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
  return NULL;
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
