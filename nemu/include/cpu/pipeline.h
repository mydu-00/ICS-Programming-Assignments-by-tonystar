/*
 * pipeline.h — Cycle-accurate 5-stage RISC-V pipeline model
 *
 * 经典五级流水线：IF → ID → EX → MEM → WB
 *
 * ┌────┐   ┌────┐   ┌────┐   ┌─────┐   ┌────┐
 * │ IF │──▷│ ID │──▷│ EX │──▷│ MEM │──▷│ WB │
 * └────┘   └────┘   └────┘   └─────┘   └────┘
 *    │        │        │         │        │
 *    │   ◁────┴────────┴─────────┴────────┘  数据前递 (forwarding)
 *    │
 *    ◁── 分支预测器 (BHT + BTB)
 *
 * 设计原则：
 * 1. 功能正确性由原始解释器保证（ISA 语义不变）
 * 2. pipeline 仅记录时序行为（周期计数、停顿、冲刷）
 * 3. cache 模拟在 pipeline 的 IF/MEM 阶段注入额外延迟
 */

#ifndef __CPU_PIPELINE_H__
#define __CPU_PIPELINE_H__

#include <common.h>
#include <cpu/decode.h>

/* ========================================================================
 * 流水线级间寄存器（Pipeline Latch）
 *
 * 真实 CPU 中，每两个相邻流水线级之间有一组锁存器（latch / pipeline register），
 * 用来在时钟上升沿捕获上一级的输出，作为下一级的输入。
 * 这里我们用 C 结构体来模拟这些级间寄存器的内容。
 * ======================================================================== */

/* IF 需要的信息：是否有效、PC */
typedef struct {
  bool     valid;       /* 此槽是否包含有效指令（气泡=false） */
  vaddr_t  pc;          /* 指令的 PC */
  uint32_t inst;        /* 取到的原始指令字 */
} PipeLatch_IF_ID;

/* ID 解码后的结果 */
typedef struct {
  bool     valid;
  vaddr_t  pc;
  uint32_t inst;

  /* 解码出的字段 */
  int      rd, rs1, rs2;
  word_t   rs1_val, rs2_val;  /* 从寄存器堆读出的值（可能被前递覆盖） */
  word_t   imm;               /* 立即数 */

  /* 控制信号 */
  bool     reg_write;    /* 是否写回寄存器 */
  bool     mem_read;     /* 是否读内存（load） */
  bool     mem_write;    /* 是否写内存（store） */
  bool     branch;       /* 是否为条件分支 */
  bool     jump;         /* 是否为无条件跳转 (jal/jalr) */
  bool     is_ecall;     /* 是否为 ecall/ebreak/mret 等系统指令 */
  bool     is_csr;       /* 是否为 CSR 指令 */

  int      alu_op;       /* ALU 操作码（见下方枚举） */
  int      mem_size;     /* 内存访问宽度: 1/2/4 */
  bool     mem_signed;   /* load 是否符号扩展 */
} PipeLatch_ID_EX;

/* EX 执行后的结果 */
typedef struct {
  bool     valid;
  vaddr_t  pc;

  int      rd;
  word_t   alu_result;   /* ALU 计算结果（或有效地址） */
  word_t   rs2_val;      /* store 时写入内存的数据 */

  bool     reg_write;
  bool     mem_read;
  bool     mem_write;
  int      mem_size;
  bool     mem_signed;

  /* 分支结果 */
  bool     branch_taken;    /* 分支是否真正跳转 */
  vaddr_t  branch_target;   /* 跳转目标地址 */
  bool     is_branch_or_jump; /* 用于分支预测验证 */
} PipeLatch_EX_MEM;

/* MEM 访存后的结果 */
typedef struct {
  bool     valid;
  vaddr_t  pc;

  int      rd;
  word_t   result;       /* 最终写回值（ALU 结果或 load 数据） */
  bool     reg_write;
} PipeLatch_MEM_WB;

/* ========================================================================
 * ALU 操作码
 * ======================================================================== */
enum {
  ALU_ADD, ALU_SUB, ALU_AND, ALU_OR, ALU_XOR,
  ALU_SLL, ALU_SRL, ALU_SRA, ALU_SLT, ALU_SLTU,
  ALU_MUL, ALU_MULH, ALU_MULHSU, ALU_MULHU,
  ALU_DIV, ALU_DIVU, ALU_REM, ALU_REMU,
  ALU_PASS_SRC2,  /* lui: result = imm */
  ALU_ADD_PC,     /* auipc: result = pc + imm */
  ALU_LINK,       /* jal/jalr: result = pc + 4 (return address) */
  ALU_NOP,
};

/* 分支比较操作码 */
enum {
  BR_NONE, BR_EQ, BR_NE, BR_LT, BR_GE, BR_LTU, BR_GEU,
};

/* ========================================================================
 * 流水线主结构体
 * ======================================================================== */
typedef struct {
  /* 级间锁存 */
  PipeLatch_IF_ID   if_id;
  PipeLatch_ID_EX   id_ex;
  PipeLatch_EX_MEM  ex_mem;
  PipeLatch_MEM_WB  mem_wb;

  /* 流水线控制 */
  bool     stall_if;     /* IF 级停顿（I-cache miss 或数据冒险导致） */
  bool     stall_id;     /* ID 级停顿（load-use 冒险） */
  bool     flush_if;     /* 冲刷 IF（分支预测失败） */
  bool     flush_id;     /* 冲刷 ID */
  bool     flush_ex;     /* 冲刷 EX */

  vaddr_t  pc_next;      /* 下一个 IF 将取指的 PC */
} PipelineState;

/* ========================================================================
 * 性能计数器
 *
 * 观测这些数字是理解微架构的核心：
 * - IPC (Instructions Per Cycle) 理想值为 1.0（单发射）
 * - 实际 IPC < 1，因为停顿和冲刷会浪费周期
 * ======================================================================== */
typedef struct {
  /* 基本计数 */
  uint64_t cycles;            /* 总时钟周期数 */
  uint64_t instructions;      /* 成功退休（WB 完成）的指令数 */

  /* 流水线事件 */
  uint64_t stall_load_use;    /* load-use 冒险导致的停顿周期 */
  uint64_t stall_icache_miss; /* I-cache miss 导致的停顿周期 */
  uint64_t stall_dcache_miss; /* D-cache miss 导致的停顿周期 */
  uint64_t flush_branch;      /* 分支预测失败导致的冲刷次数 */

  /* 分支预测 */
  uint64_t br_total;          /* 分支指令总数 */
  uint64_t br_correct;        /* 预测正确次数 */

  /* 缓存 */
  uint64_t icache_access;
  uint64_t icache_hit;
  uint64_t dcache_access;
  uint64_t dcache_hit;
} PerfCounters;

/* ========================================================================
 * 全局接口
 * ======================================================================== */

/* 初始化 pipeline + cache + branch predictor */
void pipeline_init(void);

/* 推进一个时钟周期，返回本周期是否有指令退休 */
bool pipeline_cycle(void);

/* 打印性能统计报告 */
void pipeline_stats(void);

/* 重置 pipeline 状态 */
void pipeline_reset(void);

/* cycle-accurate 模式的替代执行入口 */
void cpu_exec_pipeline(uint64_t n);

/* 访问性能计数器（只读） */
const PerfCounters *pipeline_get_perf(void);

#endif /* __CPU_PIPELINE_H__ */
