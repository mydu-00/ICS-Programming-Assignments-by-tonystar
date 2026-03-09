/*
 * cache.h — 组相联缓存模型（Set-Associative Cache Simulator）
 *
 * ┌─────────────────────────────────────────────────────────────────┐
 * │  为什么需要缓存？                                                │
 * │                                                                 │
 * │  CPU 寄存器访问 ≈ 1 周期                                        │
 * │  SRAM (L1 cache)  ≈ 1-4 周期                                   │
 * │  DRAM (主存)      ≈ 50-200 周期                                 │
 * │                                                                 │
 * │  如果每次访存都等主存响应，CPU 大部分时间在空等——                   │
 * │  流水线再怎么优化也无济于事。                                       │
 * │  缓存利用时间局部性（temporal locality）和空间局部性               │
 * │  （spatial locality）来弥合这个速度鸿沟。                         │
 * └─────────────────────────────────────────────────────────────────┘
 *
 * 本模型实现：
 * - 可配置的组数（sets）、路数（ways）、块大小（block_size）
 * - LRU 替换策略
 * - write-back + write-allocate 写策略
 * - 独立的 I-cache 和 D-cache 实例
 *
 * 地址分解：
 *   ┌──────────┬───────────┬──────────────┐
 *   │   tag    │   index   │ block offset │
 *   └──────────┴───────────┴──────────────┘
 *   index 选择哪一组（set）
 *   tag   在该组内匹配哪一路（way）
 *   offset 确定块内字节位置
 */

#ifndef __CPU_CACHE_H__
#define __CPU_CACHE_H__

#include <common.h>

/* ========================================================================
 * 缓存行（Cache Line / Block）
 *
 * 每个缓存行存储一个固定大小的内存块，加上元数据：
 * - valid: 是否包含有效数据
 * - dirty: 是否被修改过（write-back 策略需要在替换时写回主存）
 * - tag: 地址的高位部分，用于匹配
 * - lru_counter: LRU 替换时的排序依据
 * ======================================================================== */
typedef struct {
  bool     valid;
  bool     dirty;
  uint32_t tag;
  uint32_t lru_counter;  /* 值越大表示越久没被访问 */
  /* 注：我们不存储实际数据内容，只模拟命中/缺失行为 */
} CacheLine;

/* ========================================================================
 * 缓存实例
 * ======================================================================== */
typedef struct {
  const char *name;           /* "L1-I" / "L1-D" 等标识 */

  int      num_sets;          /* 组数（必须是 2 的幂） */
  int      num_ways;          /* 每组的路数（相联度） */
  int      block_size;        /* 块大小（字节，必须是 2 的幂） */

  /* 派生常量（init 时计算） */
  int      offset_bits;       /* log2(block_size) */
  int      index_bits;        /* log2(num_sets) */
  uint32_t offset_mask;
  uint32_t index_mask;

  /* 缓存命中延迟 / 缺失惩罚（周期数） */
  int      hit_latency;       /* 命中时的额外延迟（L1 通常 = 0 或 1） */
  int      miss_penalty;      /* 缺失时的额外延迟（模拟主存访问时间） */

  /* 缓存行数组 [num_sets][num_ways] */
  CacheLine *lines;

  /* 统计 */
  uint64_t accesses;
  uint64_t hits;
} Cache;

/* ========================================================================
 * 缓存操作接口
 *
 * 返回值：访问此地址的延迟周期数
 * - hit:  返回 hit_latency
 * - miss: 返回 miss_penalty（可能包括 dirty writeback 的额外开销）
 * ======================================================================== */

/* 初始化缓存实例 */
void cache_init(Cache *c, const char *name,
                int num_sets, int num_ways, int block_size,
                int hit_latency, int miss_penalty);

/* 释放缓存内存 */
void cache_free(Cache *c);

/* 读访问（IF 取指 / MEM 读） */
int cache_read(Cache *c, paddr_t addr);

/* 写访问（MEM 写） */
int cache_write(Cache *c, paddr_t addr);

/* 使整个缓存无效 */
void cache_invalidate(Cache *c);

/* 打印缓存统计信息 */
void cache_print_stats(const Cache *c);

/* ========================================================================
 * 全局缓存实例（在 cache.c 中定义）
 * ======================================================================== */
extern Cache icache;   /* L1 指令缓存 */
extern Cache dcache;   /* L1 数据缓存 */

/* 默认配置参数 */
#define ICACHE_SETS       64     /* 64 组 */
#define ICACHE_WAYS       4      /* 4 路组相联 */
#define ICACHE_BLOCK_SIZE 64     /* 64 字节/块 */
#define ICACHE_HIT_LAT    0      /* 命中 0 额外周期（流水线内吸收） */
#define ICACHE_MISS_PEN   10     /* 缺失 10 周期 */

#define DCACHE_SETS       64
#define DCACHE_WAYS       4
#define DCACHE_BLOCK_SIZE 64
#define DCACHE_HIT_LAT    0
#define DCACHE_MISS_PEN   10

#endif /* __CPU_CACHE_H__ */
