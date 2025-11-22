#include <proc.h>
#include <am.h>  // for Area, kcontext, ucontext, heap
#include <string.h>

extern void *new_page(size_t nr_page);
extern void  map(AddrSpace *as, void *va, void *pa, int prot);
extern void  protect(AddrSpace *as);

#define MAX_NR_PROC 4
#ifndef MAX_ARG
#define MAX_ARG 256
#endif
#ifndef MAX_ENV
#define MAX_ENV 256
#endif

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
    if ((j & 0x3fff) == 0) {
      Log("Hello(arg=%p) count=%d", (uintptr_t)arg, j);
    }
    yield();
  }
}

static inline Context *context_kload(PCB *p, void (*entry)(void *), void *arg) {
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = kcontext(kstack, entry, arg);
  return p->cp;
}

// 裁剪计数
static size_t count_vec_capped(char *const vec[], size_t max_n) {
  if (!vec) return 0;
  size_t n = 0;
  while (n < max_n && vec[n] != NULL) n++;
  return n;
}

// 加载用户程序并创建用户上下文
Context *context_uload(PCB *p, const char *filename,
                       char *const argv[], char *const envp[]) {
  if (!loader) {
    panic("loader() not found; please provide loader to get user entry of %s", filename);
  }

  // A) 先准备 argv/envp 的拷贝（仍在旧进程上下文里）
  size_t argc = count_vec_capped(argv, MAX_ARG);
  size_t envc = count_vec_capped(envp, MAX_ENV);

#ifdef HAS_VME
  // loader(p, ...) 内部会调用 protect(&p->as)，为该进程建立地址空间
  uintptr_t entry = loader(p, filename);
  AddrSpace *as = &p->as;

  // 为用户栈映射 32KB 到虚拟地址空间末尾 [as->area.end - 32KB, as->area.end)
  uintptr_t ustack_end   = (uintptr_t)as->area.end;
  uintptr_t ustack_start = ustack_end - 8 * PGSIZE;
  for (uintptr_t va = ustack_start; va < ustack_end; va += PGSIZE) {
    void *pa = new_page(1);
    memset(pa, 0, PGSIZE);
    map(as, (void *)va, pa, 0);
  }
  char *sp = (char *)ustack_end;   // 栈顶从虚拟地址末尾开始
#else
  // 未开启 VME：直接用物理内存当栈
  char *ustack_base = (char *)new_page(8);         // 32KB
  char *sp = ustack_base + 8 * PGSIZE;
  uintptr_t entry = loader(p, filename);
#endif

  char *argv_ptrs[MAX_ARG];
  char *envp_ptrs[MAX_ENV];

  // 在栈上从高地址向低地址拷贝字符串
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

  // 对齐
  sp = (char *)((uintptr_t)sp & ~(sizeof(uintptr_t) - 1));

  // [argc][argv...][NULL][envp...][NULL]
  size_t nwords = 1 + argc + 1 + envc + 1;
  sp -= nwords * sizeof(uintptr_t);
  uintptr_t *args_ptr = (uintptr_t *)sp;

  args_ptr[0] = (uintptr_t)argc;
  uintptr_t idx = 1;
  for (size_t i = 0; i < argc; i++) args_ptr[idx++] = (uintptr_t)argv_ptrs[i];
  args_ptr[idx++] = 0;
  for (size_t i = 0; i < envc; i++) args_ptr[idx++] = (uintptr_t)envp_ptrs[i];
  args_ptr[idx++] = 0;

  // 创建用户 Context，并把 argc 的地址放到 GPRx(a0)
#ifdef HAS_VME
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = ucontext(&p->as, kstack, (void *)entry);

  // 关键：把该进程的 satp 写入 CSR，确保首次运行用的是它的页表
  extern void __am_switch(Context *c);   // 在文件顶部声明一次
  __am_switch(p->cp);
#else
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = ucontext(NULL, kstack, (void *)entry);
#endif
  p->cp->GPRx = (uintptr_t)args_ptr;
  return p->cp;
}

void init_proc() {
  switch_boot_pcb();
  Log("Initializing processes...");

  context_kload(&pcb[0], hello_fun, (void *)1);

  char *const nterm_argv[] = { "nterm", NULL };
  char *const nterm_envp[] = { "PATH=/bin:/usr/bin", NULL };
  context_uload(&pcb[1], "/bin/nterm", nterm_argv, nterm_envp);
}

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
