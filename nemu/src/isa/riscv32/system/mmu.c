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
#include <memory/vaddr.h>
#include <memory/paddr.h>
#include <csr.h>

paddr_t isa_mmu_translate(vaddr_t vaddr, int len, int type) {
  word_t satp = csr_read(CSR_SATP);
  // satp's specific bit layout for riscv32 (Sv32):
  // bit 31 is MODE. 0 = Bare, 1 = Sv32.
  if ((satp & 0x80000000) == 0) {
    return vaddr;
  }

  uint32_t directory_ppn = (satp & 0x3fffff);
  paddr_t directory_base = directory_ppn << 12;

  // Sv32 paging:
  // vaddr: [31:22] VPN1 | [21:12] VPN0 | [11:0] Offset
  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t offset = vaddr & 0xfff;

  // Level 1 Page Table Entry
  paddr_t pte1_addr = directory_base + vpn1 * 4;
  word_t pte1 = paddr_read(pte1_addr, 4);

  if (!(pte1 & 0x1)) { // V bit
    return MEM_RET_FAIL;
  }
  
  // Level 0 Page Table Entry
  // PPN is in [31:10] of the PTE
  paddr_t pte0_base = (pte1 >> 10) << 12;
  paddr_t pte0_addr = pte0_base + vpn0 * 4;
  word_t pte0 = paddr_read(pte0_addr, 4);

  if (!(pte0 & 0x1)) { // V bit
    return MEM_RET_FAIL; 
  }

  // Physical Address: PPN of PTE0 | Offset
  paddr_t paddr = ((pte0 >> 10) << 12) | offset;
  return paddr;
}
