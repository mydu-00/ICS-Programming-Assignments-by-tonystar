/*
 * pipeline.h -- 真正周期精确的五级 RISC-V 流水线
 *
 * Execute-in-Pipeline 架构:
 *   每条指令在 IF/ID/EX/MEM/WB 五个时钟周期中依次完成各自阶段的工作。
 *   寄存器堆仅在 WB 级写入, 保证 mispredicted 路径上的指令不污染
 *   架构状态。cache 存储真实数据, IF 从 I-cache 读指令,
 *   MEM 从 D-cache 读写数据。
 *
 *   不再依赖 isa_exec_once() -- pipeline 本身就是执行引擎。
 */

#ifndef __CPU_PIPELINE_H__
#define __CPU_PIPELINE_H__

#include <common.h>

/* ================================================================
 * 级间锁存器 (Pipeline Latch)
 * ================================================================ */

typedef struct {
  bool     valid;
  vaddr_t  pc;
  uint32_t inst;
  /* 分支预测快照 (沿管线传递, 供 EX 级验证) */
  bool     pred_taken;
  vaddr_t  pred_target;
} PipeLatch_IF_ID;

typedef struct {
  bool     valid;
  vaddr_t  pc;
  uint32_t inst;

  int      rd, rs1, rs2;
  word_t   rs1_val, rs2_val;
  word_t   imm;

  bool     reg_write;
  bool     mem_read;
  bool     mem_write;
  bool     branch;
  bool     jump;
  bool     is_system;    /* ecall/ebreak/mret/csr 等需要序列化的指令 */

  int      alu_op;
  int      mem_size;
  bool     mem_signed;

  bool     pred_taken;
  vaddr_t  pred_target;
} PipeLatch_ID_EX;

typedef struct {
  bool     valid;
  vaddr_t  pc;
  uint32_t inst;

  int      rd;
  word_t   alu_result;
  word_t   store_data;   /* store 时要写入内存的值 */

  bool     reg_write;
  bool     mem_read;
  bool     mem_write;
  int      mem_size;
  bool     mem_signed;

  bool     branch_taken;
  vaddr_t  branch_target;
  bool     is_branch_or_jump;
} PipeLatch_EX_MEM;

typedef struct {
  bool     valid;
  vaddr_t  pc;

  int      rd;
  word_t   result;
  bool     reg_write;
} PipeLatch_MEM_WB;

/* ================================================================
 * ALU / Branch 操作码
 * ================================================================ */
enum {
  ALU_ADD, ALU_SUB, ALU_AND, ALU_OR, ALU_XOR,
  ALU_SLL, ALU_SRL, ALU_SRA, ALU_SLT, ALU_SLTU,
  ALU_MUL, ALU_MULH, ALU_MULHSU, ALU_MULHU,
  ALU_DIV, ALU_DIVU, ALU_REM, ALU_REMU,
  ALU_PASS_SRC2,
  ALU_ADD_PC,
  ALU_LINK,
  ALU_NOP,
};

enum {
  BR_NONE, BR_EQ, BR_NE, BR_LT, BR_GE, BR_LTU, BR_GEU,
};

/* ================================================================
 * 流水线主结构 & 性能计数器
 * ================================================================ */
typedef struct {
  PipeLatch_IF_ID   if_id;
  PipeLatch_ID_EX   id_ex;
  PipeLatch_EX_MEM  ex_mem;
  PipeLatch_MEM_WB  mem_wb;

  bool     stall_if;
  bool     stall_id;
  bool     flush_if;
  bool     flush_id;

  vaddr_t  pc_next;
} PipelineState;

typedef struct {
  uint64_t cycles;
  uint64_t instructions;

  uint64_t stall_load_use;
  uint64_t stall_icache_miss;
  uint64_t stall_dcache_miss;
  uint64_t flush_branch;

  uint64_t br_total;
  uint64_t br_correct;

  uint64_t icache_access;
  uint64_t icache_hit;
  uint64_t dcache_access;
  uint64_t dcache_hit;
} PerfCounters;

/* ================================================================
 * 接口
 * ================================================================ */
void pipeline_init(void);
bool pipeline_cycle(void);
void pipeline_stats(void);
void pipeline_reset(void);
void cpu_exec_pipeline(uint64_t n);
const PerfCounters *pipeline_get_perf(void);

#endif /* __CPU_PIPELINE_H__ */
