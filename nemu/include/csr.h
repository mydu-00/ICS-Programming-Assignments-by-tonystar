#ifndef __CSR_H__
#define __CSR_H__

#include <common.h>

#define CSR_MEPC    0x341
#define CSR_MCAUSE  0x342
#define CSR_MSTATUS 0x300
#define CSR_MTVEC   0x305
#define CSR_SATP    0x180   // Sv32 satp

extern word_t csr_mepc, csr_mcause, csr_mstatus, csr_mtvec;

static inline word_t csr_read(int csr) {
  switch (csr) {
    case CSR_MEPC:    return csr_mepc;
    case CSR_MCAUSE:  return csr_mcause;
    case CSR_MSTATUS: return csr_mstatus;
    case CSR_MTVEC:   return csr_mtvec;
    default: return 0;
  }
}

static inline void csr_write(int csr, word_t val) {
  switch (csr) {
    case CSR_MEPC:    csr_mepc = val; break;
    case CSR_MCAUSE:  csr_mcause = val; break;
    case CSR_MSTATUS: csr_mstatus = val; break;
    case CSR_MTVEC:   csr_mtvec = val; break;
    default: break;
  }
}

#endif