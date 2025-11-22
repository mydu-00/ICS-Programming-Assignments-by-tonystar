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
#include <memory/paddr.h>
#include <memory/vaddr.h>  // 确保包含了 MMU_xxx 常量声明

word_t vaddr_ifetch(vaddr_t addr, int len) {
  int type = MEM_TYPE_IFETCH;
  int mmu = isa_mmu_check(addr, len, type);
  if (mmu == MMU_DIRECT) {
    return paddr_read(addr, len);
  } else if (mmu == MMU_TRANSLATE) {
    paddr_t pa = isa_mmu_translate(addr, len, type);
    return paddr_read(pa, len);
  } else {
    // MMU_FAIL 不会在 PA 中出现
    assert(0);
    return 0;
  }
}

word_t vaddr_read(vaddr_t addr, int len) {
  int type = MEM_TYPE_READ;
  int mmu = isa_mmu_check(addr, len, type);
  if (mmu == MMU_DIRECT) {
    return paddr_read(addr, len);
  } else if (mmu == MMU_TRANSLATE) {
    paddr_t pa = isa_mmu_translate(addr, len, type);
    return paddr_read(pa, len);
  } else {
    assert(0);
    return 0;
  }
}

void vaddr_write(vaddr_t addr, int len, word_t data) {
  int type = MEM_TYPE_WRITE;
  int mmu = isa_mmu_check(addr, len, type);
  if (mmu == MMU_DIRECT) {
    paddr_write(addr, len, data);
  } else if (mmu == MMU_TRANSLATE) {
    paddr_t pa = isa_mmu_translate(addr, len, type);
    paddr_write(pa, len, data);
  } else {
    assert(0);
  }
}
