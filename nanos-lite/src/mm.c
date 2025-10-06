#include <common.h>
#include "/home/tony/codingproj/ics2025/abstract-machine/am/include/am.h"
// #include <nemu.h>
#define PGSIZE    4096
static void *pf = NULL;
static uintptr_t brk_curr = 0;  // 记录当前 program break

void* new_page(size_t nr_page) {
  void *ret = pf;
  pf = (void *)((uintptr_t)pf + nr_page * PGSIZE);
  return ret;
}

#ifdef HAS_VME
static void* pg_alloc(int n) {
  return new_page((n + PGSIZE - 1) / PGSIZE);
}
#endif

void free_page(void *p) {
  panic("free_page() not implemented");
}

int mm_brk(uintptr_t brk) {
  if (brk == 0) return 0;
  if (brk_curr == 0) brk_curr = (uintptr_t)pf;
  if (brk > brk_curr) {
    // 需要分配 (brk - brk_curr) 向上取整的页
    uintptr_t need = brk - brk_curr;
    size_t pages = (need + PGSIZE - 1) / PGSIZE;
    new_page(pages);
    brk_curr = brk;
  }
  return 0;
}

void init_mm() {
  pf = (void *)ROUNDUP(heap.start, PGSIZE);
  brk_curr = (uintptr_t)pf;
  Log("free physical pages starting from %p", pf);
#ifdef HAS_VME
  vme_init(pg_alloc, free_page);
#endif
}
