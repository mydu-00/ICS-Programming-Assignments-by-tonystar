/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <isa-def.h>
#include <csr.h>
#include <memory/vaddr.h>
#include <memory/paddr.h>

// Sv32 PTE flag bits（与 AM 侧 <arch/riscv.h> 保持一致）
#define PTE_V 0x001
#define PTE_R 0x002
#define PTE_W 0x004
#define PTE_X 0x008
#define PTE_U 0x010
#define PTE_G 0x020
#define PTE_A 0x040
#define PTE_D 0x080

// 这些常量一般在 memory/vaddr.h 或 common.h 中定义，若没有可以自己定义：
#ifndef MMU_DIRECT
#define MMU_DIRECT    0
#endif
#ifndef MMU_TRANSLATE
#define MMU_TRANSLATE 1
#endif
#ifndef MMU_FAIL
#define MMU_FAIL      2
#endif

// 根据 satp.MODE 判断是否开启分页
int isa_mmu_check(vaddr_t vaddr, int len, int type) {
  word_t satp = csr_read(CSR_SATP);
  uint32_t mode = satp >> 31;
  return mode ? MMU_TRANSLATE : MMU_DIRECT;
}

paddr_t isa_mmu_translate(vaddr_t vaddr, int len, int type) {
  (void)len; (void)type;

  word_t satp = csr_read(CSR_SATP);
  uint32_t mode = satp >> 31;
  if (mode == 0) {
    // 未开启分页，直接把 vaddr 当物理地址
    return vaddr;
  }

  // Sv32: satp[30:0] = PPN(root)
  paddr_t root_ppn = satp & ((1u << 31) - 1);
  paddr_t root = root_ppn << 12;   // 根页表物理地址

  // 解析虚拟地址：VPN[1], VPN[0], offset
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  uint32_t off  = vaddr & 0xfff;

  // 一级页表项
  paddr_t pte1_pa = root + vpn1 * 4;
  uint32_t pte1 = paddr_read(pte1_pa, 4);
  assert(pte1 & PTE_V);  // 必须有效，否则说明 VME map 错

  paddr_t pt_base = (paddr_t)(pte1 >> 10) << 12;

  // 二级页表项
  paddr_t pte0_pa = pt_base + vpn0 * 4;
  uint32_t pte0 = paddr_read(pte0_pa, 4);
  assert(pte0 & PTE_V);

  paddr_t ppn = pte0 >> 10;
  paddr_t pa  = (ppn << 12) | off;

  return pa;
}
