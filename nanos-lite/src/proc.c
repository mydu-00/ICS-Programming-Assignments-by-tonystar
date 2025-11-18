#include <proc.h>
#include <am.h>  // for Area, kcontext

#define MAX_NR_PROC 4

static PCB pcb[MAX_NR_PROC] __attribute__((used)) = {};
static PCB pcb_boot = {};
PCB *current = NULL;

/* declare loader entry so we can call it here */
extern void naive_uload(PCB *pcb, const char *filename);

void switch_boot_pcb() {
  current = &pcb_boot;
}

void hello_fun(void *arg) {
  int j = 1;
  while (1) {
    // 打印不同的参数以区分不同内核线程
    Log("Hello World from Nanos-lite with arg '%p' for the %dth time!", (uintptr_t)arg, j);
    j ++;
    yield();
  }
}

// 在 PCB 的栈上创建内核上下文，并记录到 pcb->cp
static inline Context *context_kload(PCB *p, void (*entry)(void *), void *arg) {
  // 假定 PCB 中有内核栈数组 p->stack
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = kcontext(kstack, entry, arg);
  return p->cp;
}

void init_proc() {
  switch_boot_pcb();

  Log("Initializing processes...");

  // 创建两个内核线程，传入不同参数
  context_kload(&pcb[0], hello_fun, (void *)1);
  context_kload(&pcb[1], hello_fun, (void *)2);

  // 不再加载用户程序；让调度器在首次 yield 时切到 pcb[0]/pcb[1]
  // naive_uload(current, "/bin/nterm");
}

// 简单的双线程轮转调度
Context* schedule(Context *prev) {
  if (current == NULL) current = &pcb_boot;

  // 保存当前上下文
  current->cp = prev;

  // 选择下一个可运行的 PCB
  PCB *next;
  if (current == &pcb_boot) {
    next = &pcb[0];
  } else {
    next = (current == &pcb[0]) ? &pcb[1] : &pcb[0];
  }

  current = next;
  return current->cp;
}
