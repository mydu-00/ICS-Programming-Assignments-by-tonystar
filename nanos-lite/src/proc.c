#include <proc.h>
#include <am.h>  // for Area, kcontext, ucontext, heap
#include <string.h>

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

// 加载用户程序并创建用户上下文；将用户栈上放置 argc/argv/envp，并把 argc 的地址放入 GPRx
static inline Context *context_uload(PCB *p, const char *filename,
                                     char *const argv[], char *const envp[]) {
  if (!loader) {
    panic("loader() not found; please provide loader to get user entry of %s", filename);
  }
  uintptr_t entry = loader(p, filename);

  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = ucontext(NULL, kstack, (void *)entry);

  // 布置用户栈：字符串区 + 指针区
  size_t argc = 0, envc = 0, i = 0;
  if (argv) while (argv[argc]) argc++;
  if (envp) while (envp[envc]) envc++;

  // 先拷贝字符串
  char *sp = (char *)heap.end;
  char *argv_ptrs[64];
  char *envp_ptrs[64];
  assert(argc < 64 && envc < 64);

  for (i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    sp -= len;
    memcpy(sp, argv[i], len);
    argv_ptrs[i] = sp;
  }
  for (i = 0; i < envc; i++) {
    size_t len = strlen(envp[i]) + 1;
    sp -= len;
    memcpy(sp, envp[i], len);
    envp_ptrs[i] = sp;
  }

  // 指针对齐
  sp = (char *)((uintptr_t)sp & ~(sizeof(uintptr_t) - 1));

  // 分配指针数组区
  size_t nwords = 1 /*argc*/ + argc + 1 /*argv NULL*/ + envc + 1 /*envp NULL*/;
  sp -= nwords * sizeof(uintptr_t);
  uintptr_t *ustack = (uintptr_t *)sp;

  // 填充 [argc][argv...][NULL][envp...][NULL]
  ustack[0] = (uintptr_t)argc;
  uintptr_t idx = 1;
  for (i = 0; i < argc; i++) ustack[idx++] = (uintptr_t)argv_ptrs[i];
  ustack[idx++] = 0;
  for (i = 0; i < envc; i++) ustack[idx++] = (uintptr_t)envp_ptrs[i];
  ustack[idx++] = 0;
  assert(idx == nwords);

  // 将 argc 的地址放到 GPRx；_start 会设置 sp=GPRx，并把该指针传给 call_main
  p->cp->GPRx = (uintptr_t)ustack;

  return p->cp;
}

void init_proc() {
  switch_boot_pcb();
  Log("Initializing processes...");

  //context_kload(&pcb[0], hello_fun, (void *)1);

  char *const pal_argv[] = { "--skip" };
  char *const pal_envp[] = { NULL };
  context_uload(&pcb[1], "/bin/pal", pal_argv, pal_envp);
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
