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

/* Common RISC-V mstatus bit positions used below (if your tree already
   defines constants like MSTATUS_MIE / MSTATUS_MPIE you can use them). */
#ifndef MSTATUS_MIE
#define MSTATUS_MIE  (1u << 3)
#endif
#ifndef MSTATUS_MPIE
#define MSTATUS_MPIE (1u << 7)
#endif

word_t csr_mepc = 0, csr_mcause = 0, csr_mstatus = 0, csr_mtvec = 0;

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

  /* update mstatus: MPIE := MIE ; MIE := 0 */
  word_t m = csr_read(CSR_MSTATUS);
  word_t mie = (m & MSTATUS_MIE) ? 1 : 0;
  m = (m & ~MSTATUS_MIE);
  if (mie) m |= MSTATUS_MPIE;
  else m &= ~MSTATUS_MPIE;
  csr_write(CSR_MSTATUS, m);

  /* return mtvec as the exception/interrupt vector */
  return csr_read(CSR_MTVEC);
}

word_t isa_query_intr() {
  return INTR_EMPTY;
}
