/*
 * cache.h -- 周期精确的组相联缓存模型
 *
 * 与之前 metadata-only 版本的关键区别：
 *   本模型存储实际数据。IF 级从 I-cache 读取指令字，MEM 级从 D-cache
 *   读/写数据。cache miss 时从 pmem[] 填充整个 block，dirty 替换时
 *   将 block 写回 pmem[]。这使得 cache 成为数据通路的一部分，
 *   而不仅仅是一个延迟计数器。
 *
 * 地址分解 (以 64 组 x 64B block 为例):
 *   addr = 0x80001234
 *   offset = addr[ 5:0] = 0x34  (块内偏移)
 *   index  = addr[11:6] = 0x48  (选组)
 *   tag    = addr[31:12]        (组内匹配)
 */

#ifndef __CPU_CACHE_H__
#define __CPU_CACHE_H__

#include <common.h>

/* 最大块大小 (字节) -- 用于静态数组; 实际大小由 init 参数决定 */
#define CACHE_MAX_BLOCK_SIZE 64

typedef struct {
  bool     valid;
  bool     dirty;
  uint32_t tag;
  uint32_t lru_counter;
  uint8_t  data[CACHE_MAX_BLOCK_SIZE];  /* 存储实际数据 */
} CacheLine;

typedef struct {
  const char *name;

  int      num_sets;
  int      num_ways;
  int      block_size;

  int      offset_bits;
  int      index_bits;
  uint32_t offset_mask;
  uint32_t index_mask;

  int      hit_latency;
  int      miss_penalty;

  CacheLine *lines;

  uint64_t accesses;
  uint64_t hits;
} Cache;

/* 初始化 / 释放 */
void cache_init(Cache *c, const char *name,
                int num_sets, int num_ways, int block_size,
                int hit_latency, int miss_penalty);
void cache_free(Cache *c);

/*
 * cache_read_data: 从 cache 读取 len 字节到 *out_data (host byte order)
 *   返回延迟周期数 (hit → hit_latency; miss → miss_penalty + 可能的 writeback)
 *
 * cache_write_data: 将 data 写入 cache 对应位置
 *   采用 write-back + write-allocate
 *   返回延迟
 *
 * cache_fetch_inst: 专用于 I-cache 的 4 字节取指, 返回指令字
 *   out_latency 输出延迟
 */
int  cache_read_data(Cache *c, paddr_t addr, int len, word_t *out_data);
int  cache_write_data(Cache *c, paddr_t addr, int len, word_t data);
uint32_t cache_fetch_inst(Cache *c, paddr_t addr, int *out_latency);

void cache_invalidate(Cache *c);
void cache_print_stats(const Cache *c);

/* 全局实例 */
extern Cache icache;
extern Cache dcache;

/* 默认配置 */
#define ICACHE_SETS       64
#define ICACHE_WAYS       4
#define ICACHE_BLOCK_SIZE 64
#define ICACHE_HIT_LAT    0
#define ICACHE_MISS_PEN   10

#define DCACHE_SETS       64
#define DCACHE_WAYS       4
#define DCACHE_BLOCK_SIZE 64
#define DCACHE_HIT_LAT    0
#define DCACHE_MISS_PEN   10

#endif /* __CPU_CACHE_H__ */
