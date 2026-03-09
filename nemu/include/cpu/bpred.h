/*
 * bpred.h — 分支预测器模型（Branch Predictor）
 *
 * ┌──────────────────────────────────────────────────────────────────┐
 * │  为什么需要分支预测？                                             │
 * │                                                                  │
 * │  流水线的 IF 级在取下一条指令时，上一条分支指令还在 EX 级——         │
 * │  分支结果尚未算出。如果等分支结果再取指，每个分支指令都会             │
 * │  浪费 2 个周期（pipeline bubble）。                              │
 * │                                                                  │
 * │  分支预测器在 IF 级就"猜"这条分支会不会跳转、跳到哪里，             │
 * │  让流水线继续推进。如果猜对了，零开销；如果猜错了，                   │
 * │  需要冲刷（flush）已经错误取出的指令，代价 = 流水线深度。           │
 * │                                                                  │
 * │  现代处理器的分支预测正确率可达 95-99%。                           │
 * └──────────────────────────────────────────────────────────────────┘
 *
 * 本模型实现两个组件：
 *
 * 1. BHT (Branch History Table) — 方向预测
 *    - 2-bit 饱和计数器数组，按 PC 低位索引
 *    - 每个计数器有 4 个状态：
 *      00 = Strongly Not Taken (SNT)
 *      01 = Weakly Not Taken   (WNT)
 *      10 = Weakly Taken       (WT)
 *      11 = Strongly Taken     (ST)
 *    - 预测时：计数器 >= 2 → 预测跳转
 *    - 更新时：实际跳转 → 计数器++，否则 → 计数器--
 *    - 2-bit 方案比 1-bit 更抗"抖动"（如交替跳/不跳的循环边界）
 *
 * 2. BTB (Branch Target Buffer) — 目标地址预测
 *    - 小型全相联/组相联缓存，存储 (PC → target) 映射
 *    - IF 级查 BHT 后，如果预测跳转，从 BTB 取目标地址
 *    - BTB miss 时回退到 PC+4
 */

#ifndef __CPU_BPRED_H__
#define __CPU_BPRED_H__

#include <common.h>

/* ========================================================================
 * 2-bit 饱和计数器状态
 * ======================================================================== */
enum {
  BP_SNT = 0,    /* Strongly Not Taken */
  BP_WNT = 1,    /* Weakly Not Taken */
  BP_WT  = 2,    /* Weakly Taken */
  BP_ST  = 3,    /* Strongly Taken */
};

/* ========================================================================
 * BHT 条目 — 就是一个 2-bit 计数器
 * ======================================================================== */

/* ========================================================================
 * BTB 条目
 * ======================================================================== */
typedef struct {
  bool     valid;
  vaddr_t  tag;        /* PC 的高位（用于匹配） */
  vaddr_t  target;     /* 上次跳转的目标地址 */
} BTBEntry;

/* ========================================================================
 * 分支预测器
 * ======================================================================== */
typedef struct {
  /* BHT */
  uint8_t *bht;         /* 2-bit 计数器数组，每个 entry 一个 uint8_t */
  int      bht_size;    /* BHT 条目数（必须是 2 的幂） */
  int      bht_mask;    /* bht_size - 1 */

  /* BTB */
  BTBEntry *btb;
  int       btb_size;   /* BTB 条目数 */
  int       btb_mask;

  /* 统计 */
  uint64_t predictions;
  uint64_t correct;
} BranchPredictor;

/* ========================================================================
 * 预测结果结构体
 * ======================================================================== */
typedef struct {
  bool    taken;         /* 预测是否跳转 */
  vaddr_t target;        /* 预测的目标地址（taken 时有意义） */
  bool    btb_hit;       /* BTB 是否命中 */
} BPrediction;

/* ========================================================================
 * 接口
 * ======================================================================== */

/* 初始化分支预测器 */
void bpred_init(BranchPredictor *bp, int bht_size, int btb_size);

/* 释放资源 */
void bpred_free(BranchPredictor *bp);

/* IF 级：给定 PC，预测分支方向和目标 */
BPrediction bpred_predict(BranchPredictor *bp, vaddr_t pc);

/* EX 级：分支结果确定后更新预测器 */
void bpred_update(BranchPredictor *bp, vaddr_t pc,
                  bool actually_taken, vaddr_t actual_target);

/* 打印预测器统计 */
void bpred_print_stats(const BranchPredictor *bp);

/* ========================================================================
 * 全局实例（在 bpred.c 中定义）
 * ======================================================================== */
extern BranchPredictor branch_predictor;

/* 默认配置 */
#define BHT_SIZE  1024    /* 1K 条 2-bit 计数器 */
#define BTB_SIZE  256     /* 256 条 BTB */

#endif /* __CPU_BPRED_H__ */
