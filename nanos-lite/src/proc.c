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
  int j = 0;
  while (1) {
    j++;
    if ((j & 0x3fff) == 0) {  // 每 16384 次循环打印一次
      Log("Hello(arg=%p) count=%d", (uintptr_t)arg, j);
    }
    yield();
  }
}

// 在 PCB 的栈上创建内核上下文，并记录到 pcb->cp
static inline Context *context_kload(PCB *p, void (*entry)(void *), void *arg) {
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = kcontext(kstack, entry, arg);
  return p->cp;
}

// 声明 new_page()
extern void *new_page(size_t nr_page);

#ifndef MAX_ARG
#define MAX_ARG 256
#endif
#ifndef MAX_ENV
#define MAX_ENV 256
#endif

// 加载用户程序并创建用户上下文；在“新分配”的用户栈上布置 argc/argv/envp，并把 argc 的地址放入 GPRx
Context *context_uload(PCB *p, const char *filename,
                       char *const argv[], char *const envp[]) {
  if (!loader) {
    panic("loader() not found; please provide loader to get user entry of %s", filename);
  }
  uintptr_t entry = loader(p, filename);

  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = ucontext(NULL, kstack, (void *)entry);

  // 计数
  size_t argc = 0, envc = 0;
  if (argv) while (argv[argc]) argc++;
  if (envp) while (envp[envc]) envc++;

  if (argc > MAX_ARG) panic("argc=%zu exceeds MAX_ARG=%d", argc, MAX_ARG);
  if (envc > MAX_ENV) panic("envc=%zu exceeds MAX_ENV=%d", envc, MAX_ENV);

  // 分配新用户栈
  char *ustack_base = (char *)new_page(8);  // 32KB
  char *sp = ustack_base + 8 * 4096;

  // 拷贝字符串
  char *argv_ptrs[MAX_ARG];
  char *envp_ptrs[MAX_ENV];

  for (size_t i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    sp -= len;
    memcpy(sp, argv[i], len);
    argv_ptrs[i] = sp;
  }
  for (size_t i = 0; i < envc; i++) {
    size_t len = strlen(envp[i]) + 1;
    sp -= len;
    memcpy(sp, envp[i], len);
    envp_ptrs[i] = sp;
  }

  // 指针对齐
  sp = (char *)((uintptr_t)sp & ~(sizeof(uintptr_t) - 1));

  // 构造指针区 [argc][argv...][NULL][envp...][NULL]
  size_t nwords = 1 + argc + 1 + envc + 1;
  sp -= nwords * sizeof(uintptr_t);
  uintptr_t *args = (uintptr_t *)sp;

  args[0] = (uintptr_t)argc;
  uintptr_t idx = 1;
  for (size_t i = 0; i < argc; i++) args[idx++] = (uintptr_t)argv_ptrs[i];
  args[idx++] = 0;
  for (size_t i = 0; i < envc; i++) args[idx++] = (uintptr_t)envp_ptrs[i];
  args[idx++] = 0;

  p->cp->GPRx = (uintptr_t)args;
  return p->cp;
}

void init_proc() {
  switch_boot_pcb();
  Log("Initializing processes...");

  context_kload(&pcb[0], hello_fun, (void *)1);

  // 运行 NTerm，方便在里面手动输入 Busybox 命令
  char *const nterm_argv[] = { "nterm", NULL };
  char *const nterm_envp[] = { NULL };
  context_uload(&pcb[1], "/bin/nterm", nterm_argv, nterm_envp);
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
