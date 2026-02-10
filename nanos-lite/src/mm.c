#include <common.h>
#include <am.h>
#include <proc.h>   // 引入 PCB 的完整定义，里面有 AddrSpace as 和 uintptr_t max_brk
// #include <nemu.h>
#define PGSIZE    4096

static void *pf = NULL;
extern char _end;        // 链接脚本里提供

void* new_page(size_t nr_page) {
  void *ret = pf;
  pf = (void *)((uintptr_t)pf + nr_page * PGSIZE);
  return ret;
}

#ifdef HAS_VME
extern void  map(AddrSpace *as, void *va, void *pa, int prot);

// 为 VME 提供的页分配回调：按字节数 n 分配，保证 n 是页大小整数倍
static void* pg_alloc(int n) {
  int nr_page = (n + PGSIZE - 1) / PGSIZE;
  void *p = new_page(nr_page);
  memset(p, 0, nr_page * PGSIZE);  // 分配的页清零
  return p;
}
#endif

void free_page(void *p) {
  panic("free_page() not implemented");
}

int mm_brk(uintptr_t brk) {
#ifdef HAS_VME
  PCB *p = current;
  assert(p != NULL);

  if (p->max_brk == 0) {
    // 第一次调用：从当前程序末尾开始当作堆起点
    p->max_brk = (uintptr_t)&_end;
  }

  if (brk == 0 || brk <= p->max_brk) return 0;

  uintptr_t old = p->max_brk;
  uintptr_t new = brk;

  uintptr_t start = ROUNDUP(old, PGSIZE);
  uintptr_t end   = ROUNDUP(new, PGSIZE);
  for (uintptr_t addr = start; addr < end; addr += PGSIZE) {
    void *pa = new_page(1);
    memset(pa, 0, PGSIZE);
    map(&p->as, (void *)addr, pa, 0);
  }

  p->max_brk = new;
  return 0;
#else
  (void)brk;
  return 0;
#endif
}

void init_mm() {
  pf = (void *)ROUNDUP(heap.start, PGSIZE);
  Log("free physical pages starting from %p", pf);
#ifdef HAS_VME
  vme_init(pg_alloc, free_page);
#endif
}
