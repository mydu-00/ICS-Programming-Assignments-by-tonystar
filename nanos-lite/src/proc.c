#include <proc.h>
#include <am.h>  // for Area, kcontext, ucontext, heap

#define MAX_NR_PROC 4

static PCB pcb[MAX_NR_PROC] __attribute__((used)) = {};
static PCB pcb_boot = {};
PCB *current = NULL;

/* 声明装载器，返回 ELF 入口 */
extern uintptr_t loader(PCB *pcb, const char *filename) __attribute__((weak));

void switch_boot_pcb() {
  current = &pcb_boot;
}

void hello_fun(void *arg) {
  int j = 1;
  while (1) {
    Log("Hello World from Nanos-lite with arg '%p' for the %dth time!", (uintptr_t)arg, j);
    j ++;
    yield();
  }
}

// 在 PCB 的栈上创建内核上下文，并记录到 pcb->cp
static inline Context *context_kload(PCB *p, void (*entry)(void *), void *arg) {
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = kcontext(kstack, entry, arg);
  return p->cp;
}

// 加载用户程序并创建用户上下文；将用户栈顶放入 GPRx
static inline Context *context_uload(PCB *p, const char *filename) {
  if (!loader) {
    panic("loader() not found; please provide loader to get user entry of %s", filename);
  }
  // 1) 通过 loader 装载 ELF，得到入口地址
  uintptr_t entry = loader(p, filename);

  // 2) 用 PCB 的内核栈放置 Context
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  // 暂不启用分页，传 NULL；若已启用，传 p->as
  p->cp = ucontext(NULL, kstack, (void *)entry);

  // 3) 约定把用户栈顶放到 GPRx，由用户态 _start 设置 sp
  p->cp->GPRx = (uintptr_t)heap.end;

  return p->cp;
}

void init_proc() {
  switch_boot_pcb();

  Log("Initializing processes...");

  // 保留一个内核线程
  context_kload(&pcb[0], hello_fun, (void *)1);

  // 把另一个换成用户进程 /bin/pal
  context_uload(&pcb[1], "/bin/pal");

  // 首次 yield 由调度器切换
}

// 简单的双线程轮转调度
Context* schedule(Context *prev) {
  if (current == NULL) current = &pcb_boot;

  current->cp = prev;

  PCB *next;
  if (current == &pcb_boot) {
    next = &pcb[0];
  } else {
    next = (current == &pcb[0]) ? &pcb[1] : &pcb[0];
  }

  current = next;
  return current->cp;
}
