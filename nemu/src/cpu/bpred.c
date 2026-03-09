#include <generated/autoconf.h>
#ifdef CONFIG_CYCLE_ACCURATE

/*
 * bpred.c — 分支预测器实现
 *
 * 核心算法：2-bit 饱和计数器
 *
 * 状态转移图：
 *
 *   实际 Not Taken          实际 Taken
 *   ◁───────────────┐   ┌───────────────▷
 *                   │   │
 *   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
 *   │ SNT │◁──│ WNT │◁──│ WT  │◁──│ ST  │
 *   │ (0) │──▷│ (1) │──▷│ (2) │──▷│ (3) │
 *   └─────┘   └─────┘   └─────┘   └─────┘
 *       实际 Taken           实际 Not Taken
 *
 *   预测规则：计数器 >= 2 (WT/ST) → 预测 Taken
 *             计数器 <  2 (SNT/WNT) → 预测 Not Taken
 *
 * 为什么用 2-bit 而不是 1-bit？
 *   考虑一个循环执行 100 次：分支跳转 99 次，最后一次不跳。
 *   1-bit：最后一次不跳后状态翻转为 NT，下次进入循环第一次又会预测错。
 *          → 每次循环入口 + 出口都错 = 2 次错误/循环
 *   2-bit：最后一次不跳后从 ST→WT，仍然预测 Taken。
 *          → 只有循环出口错一次 = 1 次错误/循环
 *
 * BTB 用途：
 *   BHT 只预测"跳不跳"(direction)，不知道"跳到哪"(target)。
 *   BTB 缓存最近跳转过的 (PC → target) 映射，让 IF 级能立即重定位 PC。
 */

#include <cpu/bpred.h>
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 全局实例
 * ======================================================================== */
BranchPredictor branch_predictor;

/* ========================================================================
 * 初始化
 * ======================================================================== */
void bpred_init(BranchPredictor *bp, int bht_size, int btb_size) {
  bp->bht_size = bht_size;
  bp->bht_mask = bht_size - 1;
  bp->bht = (uint8_t *)calloc(bht_size, sizeof(uint8_t));
  /* 初始化所有计数器为 WNT (01)：倾向不跳转 */
  memset(bp->bht, BP_WNT, bht_size);

  bp->btb_size = btb_size;
  bp->btb_mask = btb_size - 1;
  bp->btb = (BTBEntry *)calloc(btb_size, sizeof(BTBEntry));

  bp->predictions = 0;
  bp->correct     = 0;
}

void bpred_free(BranchPredictor *bp) {
  free(bp->bht); bp->bht = NULL;
  free(bp->btb); bp->btb = NULL;
}

/* ========================================================================
 * BHT 索引：用 PC 的低位（跳过最低 2 位，因为 RISC-V 指令 4 字节对齐）
 * ======================================================================== */
static inline int bht_index(const BranchPredictor *bp, vaddr_t pc) {
  return (int)((pc >> 2) & bp->bht_mask);
}

/* BTB 索引：同理 */
static inline int btb_index(const BranchPredictor *bp, vaddr_t pc) {
  return (int)((pc >> 2) & bp->btb_mask);
}

/* BTB tag：去掉低位后的剩余部分，用于消除别名冲突 */
static inline vaddr_t btb_tag(const BranchPredictor *bp, vaddr_t pc) {
  /* index 使用了 log2(btb_size) + 2 位，tag = 其余高位 */
  int shift = 2;
  /* 简化：直接用完整 PC 做 tag 比较（空间换准确度） */
  (void)bp;
  return pc >> shift;
}

/* ========================================================================
 * 预测
 *
 * IF 级调用：给定当前 PC，返回预测结果
 * ======================================================================== */
BPrediction bpred_predict(BranchPredictor *bp, vaddr_t pc) {
  BPrediction pred;

  bp->predictions++;

  /* 查询 BHT */
  int bi = bht_index(bp, pc);
  bool direction = (bp->bht[bi] >= BP_WT);   /* >= 2 → 预测 Taken */

  /* 查询 BTB */
  int ti = btb_index(bp, pc);
  vaddr_t tag = btb_tag(bp, pc);

  if (bp->btb[ti].valid && bp->btb[ti].tag == tag) {
    pred.btb_hit = true;
    pred.target  = bp->btb[ti].target;
  } else {
    pred.btb_hit = false;
    pred.target  = pc + 4;  /* BTB miss → 只能猜 PC+4 */
  }

  /* 最终预测：方向 Taken 且 BTB hit 时才重定向 */
  pred.taken = direction && pred.btb_hit;
  if (!pred.taken) {
    pred.target = pc + 4;
  }

  return pred;
}

/* ========================================================================
 * 更新
 *
 * EX 级调用：分支结果确定后更新 BHT 和 BTB
 * ======================================================================== */
void bpred_update(BranchPredictor *bp, vaddr_t pc,
                  bool actually_taken, vaddr_t actual_target) {
  /* 更新 BHT：2-bit 饱和计数器 */
  int bi = bht_index(bp, pc);
  if (actually_taken) {
    if (bp->bht[bi] < BP_ST) bp->bht[bi]++;   /* ++, 饱和到 3 */
  } else {
    if (bp->bht[bi] > BP_SNT) bp->bht[bi]--;  /* --, 饱和到 0 */
  }

  /* 更新 BTB：只在实际跳转时写入/更新 */
  if (actually_taken) {
    int ti = btb_index(bp, pc);
    bp->btb[ti].valid  = true;
    bp->btb[ti].tag    = btb_tag(bp, pc);
    bp->btb[ti].target = actual_target;
  }
}

/* ========================================================================
 * 统计报告
 * ======================================================================== */
void bpred_print_stats(const BranchPredictor *bp) {
  printf("  [BPred] predictions=%" PRIu64 " correct=%" PRIu64
         " accuracy=%.2f%%\n",
         bp->predictions, bp->correct,
         bp->predictions > 0
             ? (double)bp->correct / bp->predictions * 100.0
             : 0.0);
}

#endif /* CONFIG_CYCLE_ACCURATE */
