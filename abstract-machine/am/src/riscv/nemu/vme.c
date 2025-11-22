#include <am.h>
#include <nemu.h>
#include <klib.h>
#include <arch/riscv.h>  // 原来是 "riscv.h"，路径不对

static AddrSpace kas = {};
static void* (*pgalloc_usr)(int) = NULL;
static void (*pgfree_usr)(void*) = NULL;
static int vme_enable = 0;

static Area segments[] = {      // Kernel memory mappings
  NEMU_PADDR_SPACE
};

#define USER_SPACE RANGE(0x40000000, 0x80000000)

static inline void set_satp(void *pdir) {
  uintptr_t mode = 1ul << (__riscv_xlen - 1);
  asm volatile("csrw satp, %0" : : "r"(mode | ((uintptr_t)pdir >> 12)));
}

static inline uintptr_t get_satp() {
  uintptr_t satp;
  asm volatile("csrr %0, satp" : "=r"(satp));
  return satp << 12;
}

bool vme_init(void* (*pgalloc_f)(int), void (*pgfree_f)(void*)) {
  pgalloc_usr = pgalloc_f;
  pgfree_usr = pgfree_f;

  kas.ptr = pgalloc_f(PGSIZE);

  int i;
  for (i = 0; i < LENGTH(segments); i ++) {
    void *va = segments[i].start;
    for (; va < segments[i].end; va += PGSIZE) {
      map(&kas, va, va, 0);
    }
  }

  set_satp(kas.ptr);
  vme_enable = 1;

  return true;
}

void protect(AddrSpace *as) {
  PTE *updir = (PTE*)(pgalloc_usr(PGSIZE));
  as->ptr = updir;
  as->area = USER_SPACE;
  as->pgsize = PGSIZE;
  // map kernel space
  memcpy(updir, kas.ptr, PGSIZE);
}

void unprotect(AddrSpace *as) {
}

void __am_get_cur_as(Context *c) {
  c->pdir = (vme_enable ? (void *)get_satp() : NULL);
}

void __am_switch(Context *c) {
  if (vme_enable && c->pdir != NULL) {
    set_satp(c->pdir);
  }
}

void map(AddrSpace *as, void *va, void *pa, int prot) {
  // 简化：prot 忽略，统一使用 R/W/X
  uintptr_t vaddr = (uintptr_t)va;
  uintptr_t paddr = (uintptr_t)pa;

  // 顶级页表基址
  PTE *root = (PTE *)as->ptr;

  // 计算 VPN
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;

  // 取出 L1 PTE
  PTE *pte1 = &root[vpn1];
  if (!(*pte1 & PTE_V)) {
    // 分配一个新的二级页表（4KB）
    void *pt = pgalloc_usr(PGSIZE);
    memset(pt, 0, PGSIZE);
    uintptr_t ppn1 = ((uintptr_t)pt >> 12);
    *pte1 = (ppn1 << 10) | PTE_V;  // 标记有效；读写执行标志由二级表项决定
  }

  // 取出 L0 页表基址
  uintptr_t pt_paddr = ((*pte1 >> 10) << 12);
  PTE *pt = (PTE *)pt_paddr;

  // 计算物理页号
  uintptr_t ppn = (paddr >> 12);

  // 写入二级 PTE：映射到 paddr 对应物理页
  PTE *pte0 = &pt[vpn0];
  *pte0 = (ppn << 10) | PTE_V | PTE_R | PTE_W | PTE_X;
}

// 创建用户上下文：仅设置 mepc，mstatus，pdir。用户栈由 Nanos-lite 放入 GPRx。
Context *ucontext(AddrSpace *as, Area kstack, void *entry) {
  uintptr_t top = (uintptr_t)kstack.end;
  Context *ctx = (Context *)(top - sizeof(Context));
  assert((uintptr_t)ctx >= (uintptr_t)kstack.start);
  memset(ctx, 0, sizeof(Context));

  // M-mode, MPP = 11
  ctx->mstatus = (uintptr_t)(3UL << 11);
  ctx->mepc = (uintptr_t)entry;

  // 记录地址空间指针（后续 __am_switch 会用到）
  ctx->pdir = as ? as->ptr : NULL;

  return ctx;
}
