#include <generated/autoconf.h>
#ifdef CONFIG_CYCLE_ACCURATE

/*
 * cache.c — 组相联缓存模拟器实现
 *
 * 缓存的核心思想：
 *   程序访问内存具有「局部性」——最近访问的地址很可能再次被访问（时间局部性），
 *   相邻地址也很可能被访问（空间局部性）。缓存就是利用这一点，
 *   在 CPU 和主存之间放一块小容量快速存储，把最近用到的数据留在里面。
 *
 * 地址分解示例（以 64 组、64B 块为例）:
 *   地址 = 0x80001234
 *   block_offset = addr[5:0]  = 0x34  (64B 块内偏移)
 *   index        = addr[11:6] = 0x48  (选择哪一组)
 *   tag          = addr[31:12]= 0x80001 (组内匹配哪一路)
 *
 * 替换策略：LRU（Least Recently Used）
 *   每次访问时，被命中的行的 lru_counter 归零，其余行加一。
 *   需要替换时，选择 lru_counter 最大（最久没访问）的行驱逐。
 */

#include <cpu/cache.h>
#include <memory/paddr.h>
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 全局实例
 * ======================================================================== */
Cache icache;
Cache dcache;

/* ========================================================================
 * 辅助：计算 log2（仅用于 2 的幂）
 * ======================================================================== */
static int log2i(int n) {
  int r = 0;
  while ((1 << r) < n) r++;
  return r;
}

/* ========================================================================
 * 初始化
 * ======================================================================== */
void cache_init(Cache *c, const char *name,
                int num_sets, int num_ways, int block_size,
                int hit_latency, int miss_penalty) {
  c->name       = name;
  c->num_sets   = num_sets;
  c->num_ways   = num_ways;
  c->block_size = block_size;
  c->hit_latency  = hit_latency;
  c->miss_penalty = miss_penalty;

  c->offset_bits = log2i(block_size);
  c->index_bits  = log2i(num_sets);
  c->offset_mask = (uint32_t)(block_size - 1);
  c->index_mask  = (uint32_t)(num_sets - 1);

  int total_lines = num_sets * num_ways;
  c->lines = (CacheLine *)calloc(total_lines, sizeof(CacheLine));

  c->accesses = 0;
  c->hits     = 0;
}

void cache_free(Cache *c) {
  if (c->lines) {
    free(c->lines);
    c->lines = NULL;
  }
}

/* ========================================================================
 * 地址分解
 * ======================================================================== */
static inline uint32_t get_tag(const Cache *c, paddr_t addr) {
  return (uint32_t)(addr >> (c->offset_bits + c->index_bits));
}

static inline uint32_t get_index(const Cache *c, paddr_t addr) {
  return (uint32_t)((addr >> c->offset_bits) & c->index_mask);
}

/* 获取某一组的第一个 CacheLine 的指针 */
static inline CacheLine *get_set(Cache *c, uint32_t index) {
  return &c->lines[index * c->num_ways];
}

/* ========================================================================
 * LRU 更新
 *
 * 当路 hit_way 被访问时：
 *   1. 同组内所有 counter <= hit_way的counter 的行，counter 不变
 *   2. 同组内所有 counter > hit_way的counter 的行，counter 不变
 *   简化做法：hit_way 的 counter 归零，同组其它行 counter 都 +1
 * ======================================================================== */
static void update_lru(CacheLine *set, int num_ways, int hit_way) {
  uint32_t hit_cnt = set[hit_way].lru_counter;
  for (int i = 0; i < num_ways; i++) {
    if (set[i].valid && set[i].lru_counter < hit_cnt) {
      set[i].lru_counter++;
    }
  }
  set[hit_way].lru_counter = 0;
}

/* 找到 LRU（counter 最大）的行索引 */
static int find_lru_victim(CacheLine *set, int num_ways) {
  /* 优先选无效行 */
  for (int i = 0; i < num_ways; i++) {
    if (!set[i].valid) return i;
  }
  /* 选 counter 最大的 */
  int victim = 0;
  uint32_t max_cnt = 0;
  for (int i = 0; i < num_ways; i++) {
    if (set[i].lru_counter > max_cnt) {
      max_cnt = set[i].lru_counter;
      victim = i;
    }
  }
  return victim;
}

/* ========================================================================
 * 查找：在指定组中查找 tag，返回路号或 -1
 * ======================================================================== */
static int find_line(CacheLine *set, int num_ways, uint32_t tag) {
  for (int i = 0; i < num_ways; i++) {
    if (set[i].valid && set[i].tag == tag) {
      return i;
    }
  }
  return -1;
}

/* ========================================================================
 * 读访问
 *
 * 返回延迟周期数：
 *   命中 → hit_latency
 *   未命中 → miss_penalty（包含：分配新行 + 从主存加载一个块的时间）
 * ======================================================================== */
int cache_read(Cache *c, paddr_t addr) {
  c->accesses++;

  uint32_t tag   = get_tag(c, addr);
  uint32_t index = get_index(c, addr);
  CacheLine *set = get_set(c, index);

  int way = find_line(set, c->num_ways, tag);
  if (way >= 0) {
    /* 命中 */
    c->hits++;
    update_lru(set, c->num_ways, way);
    return c->hit_latency;
  }

  /* 未命中：选择 victim 并替换 */
  int victim = find_lru_victim(set, c->num_ways);

  /* 如果 victim 是 dirty 的，还需要额外写回时间
   * （简化：我们把 dirty writeback 的开销合并到 miss_penalty 中） */
  int extra = 0;
  if (set[victim].valid && set[victim].dirty) {
    extra = c->miss_penalty / 2;  /* 写回半个周期（简化） */
  }

  set[victim].valid = true;
  set[victim].dirty = false;
  set[victim].tag   = tag;
  update_lru(set, c->num_ways, victim);

  return c->miss_penalty + extra;
}

/* ========================================================================
 * 写访问（Write-Back + Write-Allocate 策略）
 *
 * Write-Allocate：写未命中时，先把整个块从主存加载到缓存，再修改
 * Write-Back：写操作只修改缓存行，标记 dirty，替换时才写回主存
 *
 * 对比 Write-Through：每次写都立即写主存——简单但慢
 * ======================================================================== */
int cache_write(Cache *c, paddr_t addr) {
  c->accesses++;

  uint32_t tag   = get_tag(c, addr);
  uint32_t index = get_index(c, addr);
  CacheLine *set = get_set(c, index);

  int way = find_line(set, c->num_ways, tag);
  if (way >= 0) {
    /* 写命中：标记 dirty，更新 LRU */
    c->hits++;
    set[way].dirty = true;
    update_lru(set, c->num_ways, way);
    return c->hit_latency;
  }

  /* 写未命中：Write-Allocate — 先加载再写 */
  int victim = find_lru_victim(set, c->num_ways);

  int extra = 0;
  if (set[victim].valid && set[victim].dirty) {
    extra = c->miss_penalty / 2;
  }

  set[victim].valid = true;
  set[victim].dirty = true;  /* 写入后立即标记 dirty */
  set[victim].tag   = tag;
  update_lru(set, c->num_ways, victim);

  return c->miss_penalty + extra;
}

/* ========================================================================
 * 使整个缓存无效（如地址空间切换时）
 * ======================================================================== */
void cache_invalidate(Cache *c) {
  int total = c->num_sets * c->num_ways;
  for (int i = 0; i < total; i++) {
    c->lines[i].valid = false;
    c->lines[i].dirty = false;
    c->lines[i].lru_counter = 0;
  }
}

/* ========================================================================
 * 统计报告
 * ======================================================================== */
void cache_print_stats(const Cache *c) {
  printf("  [%s] accesses=%" PRIu64 " hits=%" PRIu64 " misses=%" PRIu64
         " hit_rate=%.2f%%\n",
         c->name,
         c->accesses, c->hits, c->accesses - c->hits,
         c->accesses > 0 ? (double)c->hits / c->accesses * 100.0 : 0.0);
}

#endif /* CONFIG_CYCLE_ACCURATE */
