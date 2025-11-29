#include <proc.h>
#include <am.h>  // for Area, kcontext, ucontext, heap
#include <string.h>
#include <common.h>
#ifdef HAS_VME
#include <arch/riscv.h>   // 如果没有定义 PTE，则手动补上
#ifndef PTE_V
typedef uintptr_t PTE;
#define PTE_V 0x001
#endif
#endif

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
void switch_boot_pcb(void) {
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

// // 裁剪计数
// static size_t count_vec_capped(char *const vec[], size_t max_n) {
//   if (!vec) return 0;
//   size_t n = 0;
//   while (n < max_n && vec[n] != NULL) n++;
//   return n;
// }

// 加载用户程序并创建用户上下文
Context *context_uload(PCB *p, const char *filename,
                       char *const argv[], char *const envp[]) {
  if (!loader) {
    panic("loader() not found; please provide loader to get user entry of %s", filename);
  }

#ifdef HAS_VME
  uintptr_t entry = loader(p, filename);
  AddrSpace *as = &p->as;

  // 统计 argc / envc
  size_t argc = 0, envc = 0;
  if (argv) while (argv[argc] && argc < MAX_ARG) argc++;
  if (envp) while (envp[envc] && envc < MAX_ENV) envc++;

  // 分配 8 页用户栈物理内存
  char *ustack_phys = (char *)new_page(8);
  memset(ustack_phys, 0, 8 * PGSIZE);

  uintptr_t ustack_end   = (uintptr_t)as->area.end;     // 0x80000000
  uintptr_t ustack_start = ustack_end - 8 * PGSIZE;     // 0x7fff8000

  // 映射虚拟栈区间
  for (int i = 0; i < 8; i++) {
    map(as,
        (void *)(ustack_start + i * PGSIZE),
        (void *)(ustack_phys + i * PGSIZE),
        0);
  }

  // sp_phys: 实际 memcpy 的物理栈指针
  // usp: 用户看到的虚拟栈指针
  char *sp_phys   = ustack_phys + 8 * PGSIZE;
  uintptr_t usp   = ustack_end;

  // 1) 先拷贝所有字符串，从高地址向低地址
  char *argv_va_buf[MAX_ARG];
  char *envp_va_buf[MAX_ENV];

  for (size_t i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    usp     -= len;
    sp_phys -= len;
    memcpy(sp_phys, argv[i], len);
    argv_va_buf[i] = (char *)usp;   // 记录虚拟地址
  }

  for (size_t i = 0; i < envc; i++) {
    size_t len = strlen(envp[i]) + 1;
    usp     -= len;
    sp_phys -= len;
    memcpy(sp_phys, envp[i], len);
    envp_va_buf[i] = (char *)usp;   // 记录虚拟地址
  }

  // 2) 对齐到字宽
  usp     &= ~(sizeof(uintptr_t) - 1);
  sp_phys  = (char *)((uintptr_t)sp_phys & ~(sizeof(uintptr_t) - 1));

  // 3) 布局 argv/envp 指针数组: [argv_ptrs...][NULL][envp_ptrs...][NULL]
  // 先 envp 再 argv 还是先 argv 再 envp，取决于你要给 call_main 什么 ABI，
  // 这里我们只保证 a1/a2 指向正确的 argv/envp 起始地址即可。

  // 布局 envp 数组
  sp_phys  -= (envc + 1) * sizeof(uintptr_t);
  uintptr_t envp_va = usp - (envc + 1) * sizeof(uintptr_t);
  uintptr_t *envp_ptrs_phys = (uintptr_t *)sp_phys;
  for (size_t i = 0; i < envc; i++) {
    envp_ptrs_phys[i] = (uintptr_t)envp_va_buf[i];
  }
  envp_ptrs_phys[envc] = 0;  // NULL 结尾

  usp = envp_va;

  // 布局 argv 数组
  sp_phys  -= (argc + 1) * sizeof(uintptr_t);
  uintptr_t argv_va = usp - (argc + 1) * sizeof(uintptr_t);
  uintptr_t *argv_ptrs_phys = (uintptr_t *)sp_phys;
  for (size_t i = 0; i < argc; i++) {
    argv_ptrs_phys[i] = (uintptr_t)argv_va_buf[i];
  }
  argv_ptrs_phys[argc] = 0;

  usp = argv_va;

  // 4) 再对齐一次，作为最终的 sp
  usp     &= ~(sizeof(uintptr_t) - 1);
  sp_phys  = (char *)((uintptr_t)sp_phys & ~(sizeof(uintptr_t) - 1));

  // 5) 创建用户 Context，设置 mepc/mstatus/pdir/sp
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  extern Context *ucontext(AddrSpace *as, Area kstack, void *entry, uintptr_t ustack_end);
  p->cp = ucontext(&p->as, kstack, (void *)entry, ustack_end);

  // 6) 设置 a0/a1/a2: argc, argv, envp（都是用户虚拟地址）
  p->cp->GPR2 = (uintptr_t)argc;    // a0
  p->cp->GPR3 = (uintptr_t)argv_va; // a1
  p->cp->GPR4 = (uintptr_t)envp_va; // a2

  Log("[ULoad] entry=%p, argc=%d, argv_va=0x%08x, ustack=[0x%08x, 0x%08x)",
      (void *)entry, (int)argc, (uint32_t)argv_va,
      (uint32_t)ustack_start, (uint32_t)ustack_end);

#else
  // 未开启 VME：直接用物理内存当栈
  char *ustack_base = (char *)new_page(8);         // 32KB
  char *sp = ustack_base + 8 * PGSIZE;
  uintptr_t entry = loader(p, filename);

  // 在栈上从高地址向低地址拷贝字符串
  for (size_t i = 0; i < argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    sp -= len;
    memcpy(sp, argv[i], len);
    argv_buf[i] = sp;   // 这里直接用物理指针作为“地址”
  }
  for (size_t i = 0; i < envc; i++) {
    size_t len = strlen(envp[i]) + 1;
    sp -= len;
    memcpy(sp, envp[i], len);
    envp_buf[i] = sp;
  }

  // 对齐
  sp = (char *)((uintptr_t)sp & ~(sizeof(uintptr_t) - 1));

  // [argc][argv...][NULL][envp...][NULL]
  size_t nwords = 1 + argc + 1 + envc + 1;
  sp -= nwords * sizeof(uintptr_t);
  uintptr_t *args_ptr = (uintptr_t *)sp;

  args_ptr[0] = (uintptr_t)argc;
  uintptr_t idx = 1;
  for (size_t i = 0; i < argc; i++) args_ptr[idx++] = (uintptr_t)argv_buf[i];
  args_ptr[idx++] = 0;
  for (size_t i = 0; i < envc; i++) args_ptr[idx++] = (uintptr_t)envp_buf[i];
  args_ptr[idx++] = 0;

  // 创建用户 Context，并把 argc 的地址放到 GPRx(a0)
  Area kstack = (Area){ p->stack, p->stack + sizeof(p->stack) };
  p->cp = ucontext(NULL, kstack, (void *)entry);
  p->cp->GPRx = (uintptr_t)args_ptr;
#endif

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
