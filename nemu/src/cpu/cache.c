#include <generated/autoconf.h>
#ifdef CONFIG_CYCLE_ACCURATE

/*
 * cache.c -- 存储真实数据的组相联缓存
 *
 * miss 时从 pmem[] (通过 guest_to_host) 填充整个 cache block;
 * dirty 替换时将 block 写回 pmem[]。
 * 这使得 cache 成为数据通路的真实组成部分。
 */

#include <cpu/cache.h>
#include <memory/paddr.h>
#include <memory/host.h>
#include <string.h>
#include <stdlib.h>

Cache icache;
Cache dcache;

static int log2i(int n) {
  int r = 0;
  while ((1 << r) < n) r++;
  return r;
}

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
  if (c->lines) { free(c->lines); c->lines = NULL; }
}

/* ---- 地址分解 ---- */
static inline uint32_t get_tag(const Cache *c, paddr_t addr) {
  return (uint32_t)(addr >> (c->offset_bits + c->index_bits));
}
static inline uint32_t get_index(const Cache *c, paddr_t addr) {
  return (uint32_t)((addr >> c->offset_bits) & c->index_mask);
}
static inline uint32_t get_offset(const Cache *c, paddr_t addr) {
  return (uint32_t)(addr & c->offset_mask);
}
static inline CacheLine *get_set(Cache *c, uint32_t index) {
  return &c->lines[index * c->num_ways];
}
/* 块对齐地址 */
static inline paddr_t block_addr(const Cache *c, paddr_t addr) {
  return addr & ~((paddr_t)c->offset_mask);
}

/* ---- LRU ---- */
static void update_lru(CacheLine *set, int num_ways, int hit_way) {
  uint32_t hit_cnt = set[hit_way].lru_counter;
  for (int i = 0; i < num_ways; i++) {
    if (set[i].valid && set[i].lru_counter < hit_cnt)
      set[i].lru_counter++;
  }
  set[hit_way].lru_counter = 0;
}

static int find_lru_victim(CacheLine *set, int num_ways) {
  for (int i = 0; i < num_ways; i++)
    if (!set[i].valid) return i;
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

static int find_line(CacheLine *set, int num_ways, uint32_t tag) {
  for (int i = 0; i < num_ways; i++)
    if (set[i].valid && set[i].tag == tag) return i;
  return -1;
}

/* ---- 块填充 / 写回 ----
 *
 * fill_block: 从 pmem[] 拷贝一整个 block 到 cache line
 * writeback_block: 将 dirty cache line 写回 pmem[]
 */
static void fill_block(Cache *c, CacheLine *line, paddr_t baddr) {
  if (in_pmem(baddr)) {
    memcpy(line->data, guest_to_host(baddr), c->block_size);
  } else {
    /* MMIO 区域不做 block 填充; 清零 */
    memset(line->data, 0, c->block_size);
  }
}

static void writeback_block(Cache *c, CacheLine *line, uint32_t index) {
  /* 重构该行的物理地址 */
  paddr_t baddr = ((paddr_t)line->tag << (c->offset_bits + c->index_bits))
                | ((paddr_t)index << c->offset_bits);
  if (in_pmem(baddr)) {
    memcpy(guest_to_host(baddr), line->data, c->block_size);
  }
}

/* ================================================================
 * 分配 cache line (miss path)
 * 返回延迟, 并将 *out_way 设为分配到的路号
 * ================================================================ */
static int allocate_line(Cache *c, paddr_t addr, uint32_t index,
                         uint32_t tag, CacheLine *set, int *out_way) {
  int victim = find_lru_victim(set, c->num_ways);
  int extra = 0;

  /* dirty writeback */
  if (set[victim].valid && set[victim].dirty) {
    writeback_block(c, &set[victim], index);
    extra = c->miss_penalty / 2;
  }

  /* 从主存填充 */
  paddr_t baddr = block_addr(c, addr);
  fill_block(c, &set[victim], baddr);

  set[victim].valid = true;
  set[victim].dirty = false;
  set[victim].tag   = tag;
  update_lru(set, c->num_ways, victim);

  *out_way = victim;
  return c->miss_penalty + extra;
}

/* ================================================================
 * 读 len 字节, 结果存入 *out_data (host byte order)
 * ================================================================ */
int cache_read_data(Cache *c, paddr_t addr, int len, word_t *out_data) {
  c->accesses++;

  uint32_t tag    = get_tag(c, addr);
  uint32_t index  = get_index(c, addr);
  uint32_t offset = get_offset(c, addr);
  CacheLine *set  = get_set(c, index);

  int way = find_line(set, c->num_ways, tag);
  int latency;

  if (way >= 0) {
    c->hits++;
    update_lru(set, c->num_ways, way);
    latency = c->hit_latency;
  } else {
    latency = allocate_line(c, addr, index, tag, set, &way);
  }

  /* 从 cache line data[] 读出 */
  CacheLine *line = &set[way];
  word_t val = 0;
  memcpy(&val, &line->data[offset], len);
  *out_data = val;
  return latency;
}

/* ================================================================
 * 写 len 字节 (write-back + write-allocate)
 * ================================================================ */
int cache_write_data(Cache *c, paddr_t addr, int len, word_t data) {
  c->accesses++;

  uint32_t tag    = get_tag(c, addr);
  uint32_t index  = get_index(c, addr);
  uint32_t offset = get_offset(c, addr);
  CacheLine *set  = get_set(c, index);

  int way = find_line(set, c->num_ways, tag);
  int latency;

  if (way >= 0) {
    c->hits++;
    update_lru(set, c->num_ways, way);
    latency = c->hit_latency;
  } else {
    latency = allocate_line(c, addr, index, tag, set, &way);
  }

  /* 写入 cache line */
  CacheLine *line = &set[way];
  memcpy(&line->data[offset], &data, len);
  line->dirty = true;
  return latency;
}

/* ================================================================
 * I-cache 取指专用: 读 4 字节指令
 * ================================================================ */
uint32_t cache_fetch_inst(Cache *c, paddr_t addr, int *out_latency) {
  word_t inst = 0;
  *out_latency = cache_read_data(c, addr, 4, &inst);
  return (uint32_t)inst;
}

/* ================================================================ */
void cache_invalidate(Cache *c) {
  int total = c->num_sets * c->num_ways;
  for (int i = 0; i < total; i++) {
    /* dirty line 先写回 */
    if (c->lines[i].valid && c->lines[i].dirty) {
      int index = i / c->num_ways;
      writeback_block(c, &c->lines[i], index);
    }
    c->lines[i].valid = false;
    c->lines[i].dirty = false;
    c->lines[i].lru_counter = 0;
  }
}

void cache_print_stats(const Cache *c) {
  printf("  [%s] accesses=%" PRIu64 " hits=%" PRIu64 " misses=%" PRIu64
         " hit_rate=%.2f%%\n",
         c->name,
         c->accesses, c->hits, c->accesses - c->hits,
         c->accesses > 0 ? (double)c->hits / c->accesses * 100.0 : 0.0);
}

#endif /* CONFIG_CYCLE_ACCURATE */
