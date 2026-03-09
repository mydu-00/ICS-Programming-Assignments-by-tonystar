#include <generated/autoconf.h>
#ifdef CONFIG_CYCLE_ACCURATE

/*
 * pipeline.c -- 真正周期精确的五级 RISC-V 流水线 (Execute-in-Pipeline)
 *
 * 关键设计:
 *   1. IF 从 I-cache 读取真实指令字 (cache 存储实际数据)
 *   2. MEM 从 D-cache 读/写真实数据
 *   3. WB 是唯一修改 cpu.gpr[] 的地方
 *   4. 被 flush 的推测指令永远不会到达 WB, 不污染架构状态
 *   5. 系统指令 (ecall/ebreak/mret/CSR) 序列化处理: 排空管线后执行
 *   6. 不依赖 isa_exec_once() -- pipeline 本身就是执行引擎
 */

#include <cpu/pipeline.h>
#include <cpu/cache.h>
#include <cpu/bpred.h>
#include <cpu/cpu.h>
#include <isa.h>
#include <memory/paddr.h>
#include <csr.h>
#include <utils.h>
#include <string.h>

void device_update();

/* ================================================================
 * 全局状态
 * ================================================================ */
static PipelineState pipe;
static PerfCounters  perf;

/* ================================================================
 * 指令字段提取
 * ================================================================ */
#define OPCODE(inst)  ((inst) & 0x7f)
#define RD(inst)      (((inst) >> 7)  & 0x1f)
#define FUNCT3(inst)  (((inst) >> 12) & 0x7)
#define RS1(inst)     (((inst) >> 15) & 0x1f)
#define RS2(inst)     (((inst) >> 20) & 0x1f)
#define FUNCT7(inst)  (((inst) >> 25) & 0x7f)

static inline word_t imm_I(uint32_t i) { return (word_t)((int32_t)i >> 20); }
static inline word_t imm_S(uint32_t i) {
  return (word_t)(((int32_t)(i & 0xfe000000) >> 20) | ((i >> 7) & 0x1f));
}
static inline word_t imm_B(uint32_t i) {
  return (word_t)(((int32_t)(i & 0x80000000) >> 19)
    | ((i & 0x80) << 4) | ((i >> 20) & 0x7e0) | ((i >> 7) & 0x1e));
}
static inline word_t imm_U(uint32_t i) { return (word_t)(i & 0xfffff000); }
static inline word_t imm_J(uint32_t i) {
  return (word_t)(((int32_t)(i & 0x80000000) >> 11)
    | (i & 0xff000) | ((i >> 9) & 0x800) | ((i >> 20) & 0x7fe));
}

/* RISC-V opcodes */
enum {
  OP_LUI=0x37, OP_AUIPC=0x17, OP_JAL=0x6f, OP_JALR=0x67,
  OP_BRANCH=0x63, OP_LOAD=0x03, OP_STORE=0x23,
  OP_ALUI=0x13, OP_ALU=0x33, OP_SYSTEM=0x73,
};

/* ================================================================
 * ID: 解码指令 -> 控制信号
 * ================================================================ */
static void decode_instruction(uint32_t inst, vaddr_t pc, PipeLatch_ID_EX *out) {
  out->inst = inst;
  out->pc   = pc;
  out->rd  = RD(inst);
  out->rs1 = RS1(inst);
  out->rs2 = RS2(inst);
  out->imm = 0;

  out->reg_write = false;
  out->mem_read = false;
  out->mem_write = false;
  out->branch = false;
  out->jump = false;
  out->is_system = false;
  out->alu_op = ALU_NOP;
  out->mem_size = 4;
  out->mem_signed = false;

  uint32_t opcode = OPCODE(inst);
  uint32_t funct3 = FUNCT3(inst);
  uint32_t funct7 = FUNCT7(inst);

  switch (opcode) {
    case OP_LUI:
      out->imm = imm_U(inst);
      out->reg_write = true;
      out->alu_op = ALU_PASS_SRC2;
      out->rs1 = 0; out->rs2 = 0;
      break;
    case OP_AUIPC:
      out->imm = imm_U(inst);
      out->reg_write = true;
      out->alu_op = ALU_ADD_PC;
      out->rs1 = 0; out->rs2 = 0;
      break;
    case OP_JAL:
      out->imm = imm_J(inst);
      out->reg_write = (out->rd != 0);
      out->jump = true;
      out->alu_op = ALU_LINK;
      out->rs1 = 0; out->rs2 = 0;
      break;
    case OP_JALR:
      out->imm = imm_I(inst);
      out->reg_write = (out->rd != 0);
      out->jump = true;
      out->alu_op = ALU_LINK;
      out->rs2 = 0;
      break;
    case OP_BRANCH:
      out->imm = imm_B(inst);
      out->branch = true;
      out->rd = 0;
      switch (funct3) {
        case 0: out->alu_op = BR_EQ;  break;
        case 1: out->alu_op = BR_NE;  break;
        case 4: out->alu_op = BR_LT;  break;
        case 5: out->alu_op = BR_GE;  break;
        case 6: out->alu_op = BR_LTU; break;
        case 7: out->alu_op = BR_GEU; break;
      }
      break;
    case OP_LOAD:
      out->imm = imm_I(inst);
      out->reg_write = true;
      out->mem_read = true;
      out->alu_op = ALU_ADD;
      out->rs2 = 0;
      switch (funct3) {
        case 0: out->mem_size = 1; out->mem_signed = true;  break;
        case 1: out->mem_size = 2; out->mem_signed = true;  break;
        case 2: out->mem_size = 4; out->mem_signed = false; break;
        case 4: out->mem_size = 1; out->mem_signed = false; break;
        case 5: out->mem_size = 2; out->mem_signed = false; break;
      }
      break;
    case OP_STORE:
      out->imm = imm_S(inst);
      out->mem_write = true;
      out->alu_op = ALU_ADD;
      out->rd = 0;
      switch (funct3) {
        case 0: out->mem_size = 1; break;
        case 1: out->mem_size = 2; break;
        case 2: out->mem_size = 4; break;
      }
      break;
    case OP_ALUI:
      out->imm = imm_I(inst);
      out->reg_write = true;
      out->rs2 = 0;
      switch (funct3) {
        case 0: out->alu_op = ALU_ADD;  break;
        case 1: out->alu_op = ALU_SLL;  break;
        case 2: out->alu_op = ALU_SLT;  break;
        case 3: out->alu_op = ALU_SLTU; break;
        case 4: out->alu_op = ALU_XOR;  break;
        case 5: out->alu_op = (funct7 == 0x20) ? ALU_SRA : ALU_SRL; break;
        case 6: out->alu_op = ALU_OR;   break;
        case 7: out->alu_op = ALU_AND;  break;
      }
      break;
    case OP_ALU:
      out->reg_write = true;
      if (funct7 == 0x01) {
        switch (funct3) {
          case 0: out->alu_op = ALU_MUL;    break;
          case 1: out->alu_op = ALU_MULH;   break;
          case 2: out->alu_op = ALU_MULHSU; break;
          case 3: out->alu_op = ALU_MULHU;  break;
          case 4: out->alu_op = ALU_DIV;    break;
          case 5: out->alu_op = ALU_DIVU;   break;
          case 6: out->alu_op = ALU_REM;    break;
          case 7: out->alu_op = ALU_REMU;   break;
        }
      } else {
        switch (funct3) {
          case 0: out->alu_op = (funct7 == 0x20) ? ALU_SUB : ALU_ADD; break;
          case 1: out->alu_op = ALU_SLL;  break;
          case 2: out->alu_op = ALU_SLT;  break;
          case 3: out->alu_op = ALU_SLTU; break;
          case 4: out->alu_op = ALU_XOR;  break;
          case 5: out->alu_op = (funct7 == 0x20) ? ALU_SRA : ALU_SRL; break;
          case 6: out->alu_op = ALU_OR;   break;
          case 7: out->alu_op = ALU_AND;  break;
        }
      }
      break;
    case OP_SYSTEM:
      out->is_system = true;
      out->rs1 = 0; out->rs2 = 0; out->rd = 0;
      break;
  }
  if (out->rd == 0) out->reg_write = false;
}

/* ================================================================
 * EX: ALU
 * ================================================================ */
static word_t execute_alu(int alu_op, word_t src1, word_t src2, vaddr_t pc) {
  switch (alu_op) {
    case ALU_ADD:     return src1 + src2;
    case ALU_SUB:     return src1 - src2;
    case ALU_AND:     return src1 & src2;
    case ALU_OR:      return src1 | src2;
    case ALU_XOR:     return src1 ^ src2;
    case ALU_SLL:     return src1 << (src2 & 0x1f);
    case ALU_SRL:     return src1 >> (src2 & 0x1f);
    case ALU_SRA:     return (word_t)((sword_t)src1 >> (src2 & 0x1f));
    case ALU_SLT:     return ((sword_t)src1 < (sword_t)src2) ? 1 : 0;
    case ALU_SLTU:    return (src1 < src2) ? 1 : 0;
    case ALU_MUL:     return src1 * src2;
    case ALU_MULH:    return (word_t)((int64_t)(sword_t)src1 * (int64_t)(sword_t)src2 >> 32);
    case ALU_MULHSU:  return (word_t)((int64_t)(sword_t)src1 * (uint64_t)src2 >> 32);
    case ALU_MULHU:   return (word_t)(((uint64_t)src1 * (uint64_t)src2) >> 32);
    case ALU_DIV:     return ((sword_t)src2==0) ? (word_t)-1 : (word_t)((sword_t)src1/(sword_t)src2);
    case ALU_DIVU:    return (src2==0) ? (word_t)-1 : src1/src2;
    case ALU_REM:     return ((sword_t)src2==0) ? src1 : (word_t)((sword_t)src1%(sword_t)src2);
    case ALU_REMU:    return (src2==0) ? src1 : src1%src2;
    case ALU_PASS_SRC2: return src2;
    case ALU_ADD_PC:  return pc + src2;
    case ALU_LINK:    return pc + 4;
    default:          return 0;
  }
}

static bool evaluate_branch(int br_op, word_t src1, word_t src2) {
  switch (br_op) {
    case BR_EQ:  return src1 == src2;
    case BR_NE:  return src1 != src2;
    case BR_LT:  return (sword_t)src1 < (sword_t)src2;
    case BR_GE:  return (sword_t)src1 >= (sword_t)src2;
    case BR_LTU: return src1 < src2;
    case BR_GEU: return src1 >= src2;
    default:     return false;
  }
}

/* ================================================================
 * 数据前递 (forwarding)
 * EX->EX (最新, 高优先) > MEM->EX
 *
 * 注意: MEM->EX 使用 saved_mem_wb (WB 刚退休的那条指令的结果),
 * 因为 stage_mem 在 stage_ex 之前运行, 已经把 mem_wb 覆写为新值。
 * ================================================================ */
static PipeLatch_MEM_WB saved_mem_wb;  /* 在 pipeline_cycle 中保存 */

static word_t forward_value(int rs, word_t reg_val) {
  if (rs == 0) return 0;

  /* EX->EX: 来自 EX/MEM latch (上一条非 load 指令) */
  if (pipe.ex_mem.valid && pipe.ex_mem.reg_write &&
      pipe.ex_mem.rd == rs && pipe.ex_mem.rd != 0 &&
      !pipe.ex_mem.mem_read) {
    if (perf.instructions < 30)
      printf("    FWD EX->EX rs=x%d from ex_mem.pc=" FMT_WORD " rd=x%d val=" FMT_WORD "\n",
             rs, pipe.ex_mem.pc, pipe.ex_mem.rd, pipe.ex_mem.alu_result);
    return pipe.ex_mem.alu_result;
  }

  /* MEM->EX: 来自上一轮 MEM/WB latch (WB 刚退休的那条指令) */
  if (saved_mem_wb.valid && saved_mem_wb.reg_write &&
      saved_mem_wb.rd == rs && saved_mem_wb.rd != 0) {
    if (perf.instructions < 30)
      printf("    FWD MEM->EX rs=x%d from saved_mem_wb.pc=" FMT_WORD " rd=x%d val=" FMT_WORD "\n",
             rs, saved_mem_wb.pc, saved_mem_wb.rd, saved_mem_wb.result);
    return saved_mem_wb.result;
  }

  if (perf.instructions < 30 && rs != 0)
    printf("    FWD NONE rs=x%d reg_val=" FMT_WORD " (ex_mem: v=%d rw=%d rd=%d mr=%d | smwb: v=%d rw=%d rd=%d)\n",
           rs, reg_val,
           pipe.ex_mem.valid, pipe.ex_mem.reg_write, pipe.ex_mem.rd, pipe.ex_mem.mem_read,
           saved_mem_wb.valid, saved_mem_wb.reg_write, saved_mem_wb.rd);

  return reg_val;
}

/* ================================================================
 * Load-use 冒险检测
 * ================================================================ */
static bool detect_load_use_hazard(void) {
  if (!pipe.id_ex.valid || !pipe.if_id.valid) return false;
  if (!pipe.id_ex.mem_read) return false;

  int load_rd = pipe.id_ex.rd;
  if (load_rd == 0) return false;

  uint32_t inst = pipe.if_id.inst;
  int rs1 = RS1(inst);
  int rs2 = RS2(inst);
  uint32_t opcode = OPCODE(inst);

  bool uses_rs1 = (opcode != OP_LUI && opcode != OP_AUIPC && opcode != OP_JAL);
  bool uses_rs2 = (opcode == OP_ALU || opcode == OP_BRANCH || opcode == OP_STORE);

  if (uses_rs1 && rs1 != 0 && rs1 == load_rd) return true;
  if (uses_rs2 && rs2 != 0 && rs2 == load_rd) return true;
  return false;
}

/* ================================================================
 * WB: 写回寄存器堆 -- 唯一修改 cpu.gpr[] 的地方
 * ================================================================ */
static bool stage_wb(void) {
  PipeLatch_MEM_WB *in = &pipe.mem_wb;
  if (!in->valid) return false;

  if (in->reg_write && in->rd != 0) {
    if (perf.instructions < 30)
      printf("  WB[%3" PRIu64 "] pc=" FMT_WORD " rd=x%d <- " FMT_WORD "\n",
             perf.instructions, in->pc, in->rd, in->result);
    cpu.gpr[in->rd] = in->result;
  }
  cpu.gpr[0] = 0;

  /* 更新 cpu.pc 为退休指令的下一条 (用于状态一致性) */
  cpu.pc = in->pc + 4;

  perf.instructions++;
  return true;
}

/* ================================================================
 * MEM: D-cache 读/写 (真实数据!)
 * ================================================================ */
static int stage_mem(void) {
  PipeLatch_EX_MEM *in  = &pipe.ex_mem;
  PipeLatch_MEM_WB *out = &pipe.mem_wb;

  if (!in->valid) { out->valid = false; return 0; }

  out->valid     = true;
  out->pc        = in->pc;
  out->rd        = in->rd;
  out->reg_write = in->reg_write;

  int latency = 0;

  if (in->mem_read) {
    paddr_t pa = (paddr_t)in->alu_result;
    word_t load_val = 0;

    if (in_pmem(pa)) {
      latency = cache_read_data(&dcache, pa, in->mem_size, &load_val);
    } else {
      /* MMIO: 直接读 */
      load_val = paddr_read(pa, in->mem_size);
      latency = DCACHE_MISS_PEN;
    }

    perf.dcache_access++;
    if (latency <= dcache.hit_latency) perf.dcache_hit++;
    else perf.stall_dcache_miss += latency;

    if (in->mem_signed) {
      switch (in->mem_size) {
        case 1: load_val = (word_t)(int32_t)(int8_t)load_val;  break;
        case 2: load_val = (word_t)(int32_t)(int16_t)load_val; break;
      }
    }
    out->result = load_val;
  } else if (in->mem_write) {
    paddr_t pa = (paddr_t)in->alu_result;

    if (in_pmem(pa)) {
      latency = cache_write_data(&dcache, pa, in->mem_size, in->store_data);
    } else {
      /* MMIO: 直接写 */
      paddr_write(pa, in->mem_size, in->store_data);
      latency = DCACHE_MISS_PEN;
    }

    perf.dcache_access++;
    if (latency <= dcache.hit_latency) perf.dcache_hit++;
    else perf.stall_dcache_miss += latency;

    out->result    = 0;
    out->reg_write = false;
  } else {
    out->result = in->alu_result;
  }

  return latency;
}

/* ================================================================
 * EX: ALU + 分支判断
 * ================================================================ */
static void stage_ex(void) {
  PipeLatch_ID_EX  *in  = &pipe.id_ex;
  PipeLatch_EX_MEM *out = &pipe.ex_mem;

  if (!in->valid) { out->valid = false; return; }

  /*
   * 重要: 先做数据前递, 再往 out (= &pipe.ex_mem) 写字段!
   * 因为 forward_value 读取 pipe.ex_mem 做 EX->EX 前递,
   * 如果先写了 out->, pipe.ex_mem 就被覆盖为当前指令的值了。
   */
  word_t src1 = forward_value(in->rs1, in->rs1_val);
  word_t src2_fwd = forward_value(in->rs2, in->rs2_val);

  if (perf.instructions < 30)
    printf("  EX pc=" FMT_WORD " inst=%08x rs1=x%d(reg=" FMT_WORD ",fwd=" FMT_WORD ") rs2=x%d(reg=" FMT_WORD ",fwd=" FMT_WORD ")\n",
           in->pc, in->inst, in->rs1, in->rs1_val, src1, in->rs2, in->rs2_val, src2_fwd);

  /* 现在可以安全写 out */
  out->valid     = true;
  out->pc        = in->pc;
  out->inst      = in->inst;
  out->rd        = in->rd;
  out->reg_write = in->reg_write;
  out->mem_read  = in->mem_read;
  out->mem_write = in->mem_write;
  out->mem_size  = in->mem_size;
  out->mem_signed = in->mem_signed;
  out->is_branch_or_jump = in->branch || in->jump;

  if (in->branch) {
    out->branch_taken = evaluate_branch(in->alu_op, src1, src2_fwd);
    out->branch_target = out->branch_taken ? (in->pc + in->imm) : (in->pc + 4);
    out->alu_result = 0;
  } else if (in->jump) {
    out->branch_taken = true;
    if (OPCODE(in->inst) == OP_JALR)
      out->branch_target = (src1 + in->imm) & ~(word_t)1;
    else
      out->branch_target = in->pc + in->imm;
    out->alu_result = execute_alu(in->alu_op, src1, in->imm, in->pc);
  } else {
    uint32_t opcode = OPCODE(in->inst);
    if (opcode == OP_ALU) {
      out->alu_result = execute_alu(in->alu_op, src1, src2_fwd, in->pc);
    } else {
      out->alu_result = execute_alu(in->alu_op, src1, in->imm, in->pc);
    }
    out->branch_taken  = false;
    out->branch_target = 0;
  }

  if (in->mem_write) {
    out->store_data = src2_fwd;
  } else {
    out->store_data = 0;
  }
}

/* ================================================================
 * 分支预测验证 (EX 级之后)
 * ================================================================ */
static void check_branch_prediction(void) {
  PipeLatch_EX_MEM *ex = &pipe.ex_mem;
  if (!ex->valid || !ex->is_branch_or_jump) return;

  /* 重查预测 (简化: 不沿锁存器传递预测快照) */
  BPrediction pred = bpred_predict(&branch_predictor, ex->pc);
  branch_predictor.predictions--;

  bool mispredicted = false;
  if (ex->branch_taken != pred.taken)
    mispredicted = true;
  else if (ex->branch_taken && ex->branch_target != pred.target)
    mispredicted = true;

  bpred_update(&branch_predictor, ex->pc,
               ex->branch_taken, ex->branch_target);
  perf.br_total++;

  if (mispredicted) {
    perf.flush_branch++;
    pipe.flush_if = true;
    pipe.flush_id = true;
    pipe.if_id.valid = false;
    pipe.id_ex.valid = false;
    pipe.pc_next = ex->branch_taken ? ex->branch_target : (ex->pc + 4);
  } else {
    perf.br_correct++;
    branch_predictor.correct++;
  }
}

/* ================================================================
 * ID: 解码 + 读寄存器堆
 * ================================================================ */
static void stage_id(void) {
  PipeLatch_IF_ID *in  = &pipe.if_id;
  PipeLatch_ID_EX *out = &pipe.id_ex;

  if (!in->valid || pipe.stall_id) {
    out->valid = false;
    return;
  }

  out->valid = true;
  decode_instruction(in->inst, in->pc, out);

  out->rs1_val = (out->rs1 != 0) ? cpu.gpr[out->rs1] : 0;
  out->rs2_val = (out->rs2 != 0) ? cpu.gpr[out->rs2] : 0;

  out->pred_taken  = in->pred_taken;
  out->pred_target = in->pred_target;
}

/* ================================================================
 * IF: I-cache 取指 + 分支预测
 * ================================================================ */
static int stage_if(void) {
  PipeLatch_IF_ID *out = &pipe.if_id;

  if (pipe.stall_if) return 0;
  if (pipe.flush_if) { out->valid = false; return 0; }

  vaddr_t pc = pipe.pc_next;
  paddr_t pa = (paddr_t)pc;

  /* 从 I-cache 取指 (真实数据) */
  int latency;
  uint32_t inst = cache_fetch_inst(&icache, pa, &latency);

  perf.icache_access++;
  if (latency <= icache.hit_latency) perf.icache_hit++;
  else perf.stall_icache_miss += latency;

  out->valid = true;
  out->pc    = pc;
  out->inst  = inst;

  /* 分支预测 */
  uint32_t opcode = OPCODE(inst);
  if (opcode == OP_BRANCH || opcode == OP_JAL || opcode == OP_JALR) {
    BPrediction pred = bpred_predict(&branch_predictor, pc);
    out->pred_taken  = pred.taken;
    out->pred_target = pred.target;
    pipe.pc_next = pred.taken ? pred.target : (pc + 4);
  } else {
    out->pred_taken  = false;
    out->pred_target = 0;
    pipe.pc_next = pc + 4;
  }

  return latency;
}

/* ================================================================
 * 系统指令序列化处理
 * ================================================================ */
static bool handle_system_inst(uint32_t inst, vaddr_t pc) {
  uint32_t funct3 = FUNCT3(inst);
  uint32_t rs2_field = RS2(inst);
  uint32_t funct7 = FUNCT7(inst);

  if (funct3 == 0) {
    if (funct7 == 0 && rs2_field == 0) {
      /* ecall */
      word_t new_pc = isa_raise_intr(11, pc);
      pipe.pc_next = new_pc;
      cpu.pc = new_pc;
      perf.instructions++;
      return true;
    } else if (funct7 == 0 && rs2_field == 1) {
      /* ebreak */
      set_nemu_state(NEMU_END, pc, cpu.gpr[10]);
      perf.instructions++;
      return true;
    } else if (funct7 == 0x18 && rs2_field == 2) {
      /* mret */
      word_t new_pc = csr_read(CSR_MEPC);
      pipe.pc_next = new_pc;
      cpu.pc = new_pc;
      word_t m = csr_read(CSR_MSTATUS);
      word_t mpie = (m >> 7) & 1;
      if (mpie) m |= (1 << 3); else m &= ~(1 << 3);
      csr_write(CSR_MSTATUS, m);
      perf.instructions++;
      return true;
    }
  } else {
    /* CSR 指令 */
    int rd  = RD(inst);
    int rs1 = RS1(inst);
    word_t rs1_val = (rs1 != 0) ? cpu.gpr[rs1] : 0;
    word_t csr_num = (inst >> 20) & 0xfff;

    switch (funct3) {
      case 1: { /* csrrw */
        word_t old = csr_read(csr_num);
        csr_write(csr_num, rs1_val);
        if (rd != 0) cpu.gpr[rd] = old;
        break;
      }
      case 2: { /* csrrs */
        word_t old = csr_read(csr_num);
        if (rs1 != 0) csr_write(csr_num, old | rs1_val);
        if (rd != 0) cpu.gpr[rd] = old;
        break;
      }
      default: {
        word_t old = csr_read(csr_num);
        if (rd != 0) cpu.gpr[rd] = old;
        break;
      }
    }
    cpu.gpr[0] = 0;
    pipe.pc_next = pc + 4;
    cpu.pc = pc + 4;
    perf.instructions++;
    return true;
  }
  return false;
}

/* ================================================================
 * 一个时钟周期 (逆序: WB -> MEM -> EX -> check -> hazard -> ID -> IF)
 * ================================================================ */
bool pipeline_cycle(void) {
  perf.cycles++;

  pipe.stall_if = false;
  pipe.stall_id = false;
  pipe.flush_if = false;
  pipe.flush_id = false;

  bool retired = stage_wb();
  saved_mem_wb = pipe.mem_wb;  /* 保存给 forward_value 的 MEM->EX 路径 */
  stage_mem();
  stage_ex();
  check_branch_prediction();

  if (detect_load_use_hazard()) {
    pipe.stall_if = true;
    pipe.stall_id = true;
    pipe.id_ex.valid = false;
    perf.stall_load_use++;
  }

  if (!pipe.stall_id && !pipe.flush_id)
    stage_id();
  else if (pipe.flush_id)
    pipe.id_ex.valid = false;

  if (!pipe.stall_if && !pipe.flush_if)
    stage_if();
  else if (pipe.flush_if)
    pipe.if_id.valid = false;

  return retired;
}

/* ================================================================
 * 初始化
 * ================================================================ */
void pipeline_init(void) {
  memset(&pipe, 0, sizeof(pipe));
  memset(&perf, 0, sizeof(perf));
  pipe.pc_next = cpu.pc;

  cache_init(&icache, "L1-I",
             ICACHE_SETS, ICACHE_WAYS, ICACHE_BLOCK_SIZE,
             ICACHE_HIT_LAT, ICACHE_MISS_PEN);
  cache_init(&dcache, "L1-D",
             DCACHE_SETS, DCACHE_WAYS, DCACHE_BLOCK_SIZE,
             DCACHE_HIT_LAT, DCACHE_MISS_PEN);
  bpred_init(&branch_predictor, BHT_SIZE, BTB_SIZE);
}

void pipeline_reset(void) {
  memset(&pipe, 0, sizeof(pipe));
  pipe.pc_next = cpu.pc;
  cache_invalidate(&icache);
  cache_invalidate(&dcache);
}

/* ================================================================
 * 排空管线: 只推进后面的级, 不取新指令
 * ================================================================ */
static void drain_pipeline(void) {
  for (int i = 0; i < 5; i++) {
    bool any_valid = pipe.if_id.valid || pipe.id_ex.valid ||
                     pipe.ex_mem.valid || pipe.mem_wb.valid;
    if (!any_valid) break;
    perf.cycles++;
    stage_wb();
    stage_mem();
    stage_ex();
    /* 不做 ID/IF, 只让已有的流下去 */
    pipe.id_ex.valid = false;
    pipe.if_id.valid = false;
  }
}

/* ================================================================
 * cpu_exec_pipeline -- 周期精确执行入口
 * ================================================================ */
void cpu_exec_pipeline(uint64_t n) {
  switch (nemu_state.state) {
    case NEMU_END: case NEMU_ABORT: case NEMU_QUIT:
      printf("Program execution has ended. To restart, exit NEMU and run again.\n");
      return;
    default: nemu_state.state = NEMU_RUNNING;
  }

  printf("[Pipeline] Cycle-accurate execute-in-pipeline mode\n");
  printf("  I-cache: %d sets x %d ways x %dB = %dKB\n",
         ICACHE_SETS, ICACHE_WAYS, ICACHE_BLOCK_SIZE,
         ICACHE_SETS * ICACHE_WAYS * ICACHE_BLOCK_SIZE / 1024);
  printf("  D-cache: %d sets x %d ways x %dB = %dKB\n",
         DCACHE_SETS, DCACHE_WAYS, DCACHE_BLOCK_SIZE,
         DCACHE_SETS * DCACHE_WAYS * DCACHE_BLOCK_SIZE / 1024);
  printf("  BHT: %d entries, BTB: %d entries\n", BHT_SIZE, BTB_SIZE);
  printf("  Pipeline: 5-stage IF->ID->EX->MEM->WB (execute-in-pipeline)\n\n");

  pipeline_init();

  uint64_t inst_limit = n;

  while (perf.instructions < inst_limit && nemu_state.state == NEMU_RUNNING) {
    /* 快速 peek: 如果管线空且下一条是 system 指令, 序列化处理 */
    if (!pipe.if_id.valid && !pipe.id_ex.valid &&
        !pipe.ex_mem.valid && !pipe.mem_wb.valid) {
      paddr_t pa = (paddr_t)pipe.pc_next;
      if (in_pmem(pa)) {
        int lat;
        uint32_t next_inst = cache_fetch_inst(&icache, pa, &lat);
        if (OPCODE(next_inst) == OP_SYSTEM) {
          perf.cycles++;
          if (handle_system_inst(next_inst, pipe.pc_next)) {
            if (nemu_state.state != NEMU_RUNNING) break;
            continue;
          }
        }
      }
    }

    /* 正常周期推进 */
    pipeline_cycle();

    /* 如果 IF 取到 system 指令, 排空后序列化 */
    if (pipe.if_id.valid && OPCODE(pipe.if_id.inst) == OP_SYSTEM) {
      vaddr_t sys_pc    = pipe.if_id.pc;
      uint32_t sys_inst = pipe.if_id.inst;
      pipe.if_id.valid  = false;

      drain_pipeline();

      perf.cycles++;
      handle_system_inst(sys_inst, sys_pc);
      if (nemu_state.state != NEMU_RUNNING) break;
      continue;
    }

    if (nemu_state.state != NEMU_RUNNING) break;
    IFDEF(CONFIG_DEVICE, device_update());
  }

  /* 最终排空 */
  drain_pipeline();

  pipeline_stats();

  switch (nemu_state.state) {
    case NEMU_RUNNING: nemu_state.state = NEMU_STOP; break;
    case NEMU_END:
      Log("nemu: %s at pc = " FMT_WORD,
          (nemu_state.halt_ret == 0 ? ANSI_FMT("HIT GOOD TRAP", ANSI_FG_GREEN)
                                    : ANSI_FMT("HIT BAD TRAP", ANSI_FG_RED)),
          nemu_state.halt_pc);
      break;
    case NEMU_ABORT:
      Log("nemu: %s at pc = " FMT_WORD,
          ANSI_FMT("ABORT", ANSI_FG_RED), nemu_state.halt_pc);
      break;
    case NEMU_QUIT: break;
  }

  cache_free(&icache);
  cache_free(&dcache);
  bpred_free(&branch_predictor);
}

/* ================================================================
 * 性能统计
 * ================================================================ */
void pipeline_stats(void) {
  printf("\n");
  printf("=============== Pipeline Performance Report ===============\n");
  printf("  Total cycles:           %" PRIu64 "\n", perf.cycles);
  printf("  Retired instructions:   %" PRIu64 "\n", perf.instructions);
  printf("  IPC (Inst/Cycle):       %.3f\n",
         perf.cycles > 0 ? (double)perf.instructions / perf.cycles : 0.0);
  printf("  CPI (Cycle/Inst):       %.3f\n",
         perf.instructions > 0 ? (double)perf.cycles / perf.instructions : 0.0);
  printf("\n--- Stall Breakdown ---\n");
  printf("  Load-use stalls:        %" PRIu64 " cycles\n", perf.stall_load_use);
  printf("  I-cache miss stalls:    %" PRIu64 " cycles\n", perf.stall_icache_miss);
  printf("  D-cache miss stalls:    %" PRIu64 " cycles\n", perf.stall_dcache_miss);
  printf("  Branch mispredictions:  %" PRIu64 " (x2 cycles = %" PRIu64 ")\n",
         perf.flush_branch, perf.flush_branch * 2);
  printf("\n--- Branch Prediction ---\n");
  printf("  Total branches:         %" PRIu64 "\n", perf.br_total);
  printf("  Correct:                %" PRIu64 "\n", perf.br_correct);
  printf("  Accuracy:               %.2f%%\n",
         perf.br_total > 0 ? (double)perf.br_correct / perf.br_total * 100.0 : 0.0);
  printf("\n--- Cache ---\n");
  cache_print_stats(&icache);
  cache_print_stats(&dcache);
  printf("==========================================================\n");
}

const PerfCounters *pipeline_get_perf(void) { return &perf; }

#endif /* CONFIG_CYCLE_ACCURATE */
