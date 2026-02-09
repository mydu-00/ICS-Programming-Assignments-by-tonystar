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
#include <csr.h>
#include <stdio.h>
#include <utils.h>
#include <generated/autoconf.h>

/* Common RISC-V mstatus bit positions used below (if your tree already
   defines constants like MSTATUS_MIE / MSTATUS_MPIE you can use them). */
#ifndef MSTATUS_MIE
#define MSTATUS_MIE  (1u << 3)
#endif
#ifndef MSTATUS_MPIE
#define MSTATUS_MPIE (1u << 7)
#endif

word_t csr_mepc = 0, csr_mcause = 0, csr_mstatus = 0, csr_mtvec = 0, csr_satp = 0;

static inline void etrace_log(word_t cause, vaddr_t epc,
                              word_t old_mstatus, word_t new_mstatus,
                              word_t mtvec) {
#if defined(CONFIG_ETRACE)
  const char *type = (cause & (1u << (sizeof(word_t)*8 - 1))) ? "INT" : "EXC";
  uint32_t code = (uint32_t)(cause & ~(1u << 31));
  printf("[ETRACE] %s code=%u mcause=0x%08x epc=0x%08x -> mtvec=0x%08x mstatus:0x%08x->0x%08x\n",
       type, code, (uint32_t)cause, (uint32_t)epc, (uint32_t)mtvec,
       (uint32_t)old_mstatus, (uint32_t)new_mstatus);
#else
  (void)cause; (void)epc; (void)old_mstatus; (void)new_mstatus; (void)mtvec;
#endif
}

word_t isa_raise_intr(word_t NO, vaddr_t epc) {
  /* Trigger an interrupt/exception:
   *  - record epc and cause
   *  - save/clear MIE -> set MPIE, clear MIE
   *  - return the mtvec (trap entry) value
   *
   * Note: this code uses the usual csr_read / csr_write helpers and a
   * global `cpu` state. If your tree uses different names, adapt accordingly.
   */

   /* save epc and cause into CPU state */
  csr_write(CSR_MEPC, epc);
  csr_write(CSR_MCAUSE, NO);

  word_t old_m = csr_read(CSR_MSTATUS);
  /* update mstatus: MPIE := MIE ; MIE := 0 */
  word_t m = old_m;
  word_t mie = (m & MSTATUS_MIE) ? 1 : 0;
  m = (m & ~MSTATUS_MIE);
  if (mie) m |= MSTATUS_MPIE;
  else m &= ~MSTATUS_MPIE;
  csr_write(CSR_MSTATUS, m);

  word_t mtvec = csr_read(CSR_MTVEC);

  // etrace (在不修改 guest 状态的情况下记录)
  etrace_log(NO, epc, old_m, m, mtvec);

  /* return mtvec as the exception/interrupt vector */
  return mtvec;
}

word_t isa_query_intr() {
  return INTR_EMPTY;
}
