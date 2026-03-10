# 周期精确处理器模拟器——设计报告与心得总结

> 任务目标：搭建一个周期精确（Cycle-Accurate）的 RISC-V 处理器模拟器，深入理解流水线、缓存、分支预测等微架构机制及其性能影响。

---

## 一、整体架构设计

### 1.1 从 Functional-First 到 Execute-in-Pipeline

模拟器经历了两个版本：

| | Functional-First（v1，已废弃） | Execute-in-Pipeline（v2，最终版） |
|---|---|---|
| 指令执行 | 调用 `isa_exec_once()` 完成功能，流水线只计时 | 流水线本身就是执行引擎，每个 stage 做真实工作 |
| Cache | 只记录 tag/valid/dirty，用于判断 hit/miss | 存储真实数据字节（`data[64]`），从 pmem 填充和写回 |
| 寄存器写 | 由 `isa_exec_once()` 在功能阶段写入 | **仅在 WB 级**写入 `cpu.gpr[]` |
| 推测执行安全性 | 功能已经执行完了，flush 只影响时序统计 | 被 flush 的指令**永远不会到达 WB**，不会污染架构状态 |
| 周期精度 | 近似——无法建模指令间的真实数据依赖 | 精确——每条指令在具体 stage 做具体的事 |

v2（Execute-in-Pipeline）的核心理念：**Pipeline IS the execution engine**。

### 1.2 五级流水线总览

```
              ┌────┐   ┌────┐   ┌────┐   ┌─────┐   ┌────┐
  pc_next ──► │ IF │──►│ ID │──►│ EX │──►│ MEM │──►│ WB │──► gpr[]
              └────┘   └────┘   └────┘   └─────┘   └────┘
                │                  │                   │
           I-cache             分支验证            唯一写回点
           取真实指令           + flush            + 更新 cpu.pc
                │                  │
           分支预测          数据前递(forwarding)
```

每两个相邻 stage 之间有一个**级间锁存器**（Pipeline Latch），用来保存上一级的输出供下一级使用：

- `IF/ID latch`：inst, pc, pred_taken, pred_target
- `ID/EX latch`：解码后的控制信号 + 寄存器值 + 立即数
- `EX/MEM latch`：ALU 结果 + 分支结果 + store_data
- `MEM/WB latch`：最终结果 + rd + reg_write

---

## 二、各 Stage 的代码逻辑

### 2.1 IF：取指令 + 分支预测

```c
static int stage_if(void) {
    vaddr_t pc = pipe.pc_next;
    paddr_t pa = (paddr_t)pc;

    // 从 I-cache 取真实指令字节
    int latency;
    uint32_t inst = cache_fetch_inst(&icache, pa, &latency);

    out->inst = inst;
    out->pc   = pc;

    // 分支预测
    if (opcode == OP_BRANCH || OP_JAL || OP_JALR) {
        BPrediction pred = bpred_predict(&branch_predictor, pc);
        pipe.pc_next = pred.taken ? pred.target : (pc + 4);
    } else {
        pipe.pc_next = pc + 4;
    }
}
```

**关键点**：
- IF 从 I-cache 读取**真实的 4 字节指令**。cache miss 时，`fill_block()` 从 `pmem[]` 拷贝一整个 64B cache block。
- 取到指令后立刻查分支预测器：如果预测跳转，`pc_next` 就指向预测目标，否则 PC+4。
- 如果预测错了，EX 级会在 2 cycle 后发现并 flush。

### 2.2 ID：指令解码 + 读寄存器堆

```c
static void stage_id(void) {
    decode_instruction(in->inst, in->pc, out);   // 生成控制信号
    out->rs1_val = cpu.gpr[out->rs1];            // 读寄存器堆
    out->rs2_val = cpu.gpr[out->rs2];
    out->pred_taken  = in->pred_taken;           // 传递预测快照
    out->pred_target = in->pred_target;
}
```

**关键点**：
- `decode_instruction()` 是一个巨大的 switch-case，根据 opcode/funct3/funct7 生成所有控制信号：`reg_write`, `mem_read`, `mem_write`, `branch`, `jump`, `is_system`, `alu_op`, `mem_size`, `mem_signed` 等。
- 从 `cpu.gpr[]`（架构寄存器堆）读取 `rs1_val` 和 `rs2_val`。这里读到的值可能是**过时的**（因为前面的指令可能还没写回），需要后续 EX 级通过 forwarding 补偿。
- 分支预测信息沿管线传递，方便 EX 级验证。

### 2.3 EX：执行 ALU + 分支判断 + 数据前递

```c
static void stage_ex(void) {
    // ⚠️ 关键：先做 forwarding，再写 out（out == &pipe.ex_mem）
    word_t src1 = forward_value(in->rs1, in->rs1_val);
    word_t src2 = forward_value(in->rs2, in->rs2_val);

    // 然后再写 out-> 字段
    out->alu_result = execute_alu(in->alu_op, src1, src2_or_imm, in->pc);

    if (in->branch) {
        out->branch_taken = evaluate_branch(in->alu_op, src1, src2);
        out->branch_target = taken ? (pc + imm) : (pc + 4);
    }
}
```

**关键点**：
- **数据前递（Forwarding）** 是 EX 级最重要的机制。`forward_value(rs, reg_val)` 检查两个来源：
  1. **EX→EX**（最高优先级）：上一条指令的 ALU 结果（在 `pipe.ex_mem` 中）
  2. **MEM→EX**：上上条指令的最终结果（在 `saved_mem_wb` 中）
- 分支指令在这里计算真实的 taken/target，然后 `check_branch_prediction()` 对比 IF 级的预测。

### 2.4 MEM：D-cache 读/写真实数据

```c
static int stage_mem(void) {
    if (in->mem_read) {
        latency = cache_read_data(&dcache, pa, len, &load_val);
        // 符号扩展
        out->result = load_val;
    } else if (in->mem_write) {
        latency = cache_write_data(&dcache, pa, len, in->store_data);
    } else {
        out->result = in->alu_result;   // 非访存指令直通
    }
}
```

**关键点**：
- Load：从 D-cache 读数据，cache miss 时触发 `allocate_line()`（可能伴随 dirty writeback），然后用 `fill_block()` 从 pmem 拷贝整个 block。
- Store：write-back + write-allocate 策略。数据写入 cache line 的 `data[]` 数组，标记 `dirty=true`。
- MMIO 地址（不在 pmem 范围内）直接调用 `paddr_read/write`，绕过 cache。

### 2.5 WB：唯一的寄存器写回点

```c
static bool stage_wb(void) {
    if (in->reg_write && in->rd != 0) {
        cpu.gpr[in->rd] = in->result;   // ← 唯一修改 gpr[] 的地方
    }
    cpu.gpr[0] = 0;                     // x0 永远为 0
    cpu.pc = in->pc + 4;
    perf.instructions++;
}
```

**关键点**：
- 这是整个模拟器中**唯一修改 `cpu.gpr[]` 的地方**。
- 这意味着被 flush 的推测指令永远不可能修改架构状态——它们在 IF/ID/EX 阶段就被清除了，根本到不了 WB。
- 这是 Execute-in-Pipeline 模型与 Functional-First 模型最本质的区别。

### 2.6 pipeline_cycle()：一个时钟周期的全过程

```c
bool pipeline_cycle(void) {
    perf.cycles++;
    // 逆序执行，模拟组合逻辑的同时读写特性
    stage_wb();                     // 1. WB 先退休，释放 gpr
    saved_mem_wb = pipe.mem_wb;     // 2. 快照给 MEM→EX forwarding
    stage_mem();                    // 3. MEM 读写 D-cache
    stage_ex();                     // 4. EX 执行 ALU + forwarding
    check_branch_prediction();      // 5. 验证分支预测
    detect_load_use_hazard();       // 6. Load-use 冒险检测
    stage_id();                     // 7. ID 解码
    stage_if();                     // 8. IF 取指
}
```

为什么是逆序？因为在真实硬件中，所有 stage 在同一个时钟沿**同时**工作。用软件模拟时，逆序确保**后面的 stage 先消费旧数据，前面的 stage 再写入新数据**，避免一个 cycle 内数据跨越多个 stage。

---

## 三、微架构机制深入理解

### 3.1 数据冒险与前递（Data Hazard & Forwarding）

**问题**：指令 B 需要读取指令 A 写入的寄存器值，但 A 还没到 WB 级。

```
        cycle 1    cycle 2    cycle 3    cycle 4    cycle 5
A:      IF         ID         EX         MEM        WB ← 写 gpr[rd]
B:                 IF         ID ← 读    EX         MEM
                               gpr[rs1]
                               （过时！）
```

**解法一：停顿（Stall）**——插入 bubble 等 A 写回。代价大（每个依赖浪费 2 cycle）。

**解法二：前递（Forwarding）**——EX 级不用 ID 读到的过时值，而是直接从管线中拿最新值：

```
A 的 EX 结果 ─────────────────────────┐
                                       ▼
B:                 IF         ID       EX（使用 forwarded 值）
```

我们实现了两条前递路径：

| 路径 | 源 | 含义 |
|------|-----|------|
| EX→EX | `pipe.ex_mem.alu_result` | 上一条指令（非 load）的 ALU 结果 |
| MEM→EX | `saved_mem_wb.result` | 上上条指令的最终结果（包括 load） |

**优先级**：EX→EX > MEM→EX > 寄存器堆值。

### 3.2 Load-Use 冒险

前递能解决大部分数据依赖，但**有一个例外**：

```
lw  x5, 0(x10)    # A: load，数据在 MEM 级才可用
add x6, x5, x7    # B: 紧接着使用 x5
```

A 的 load 结果在 MEM 级末尾才出来，但 B 在 EX 级就需要。EX→EX 前递只有 ALU 结果（load 还没完成），MEM→EX 差了一拍。**此时无法前递，必须 stall 1 cycle**。

```c
static bool detect_load_use_hazard(void) {
    if (!pipe.id_ex.mem_read) return false;
    int load_rd = pipe.id_ex.rd;
    // 检查下一条指令（在 if_id 中）是否使用了 load_rd
    if (rs1 == load_rd || rs2 == load_rd) return true;
}
```

检测到后：stall IF 和 ID 一个周期，同时把 id_ex 置为无效（插入 bubble）。Load 结果在下一 cycle 进入 MEM/WB，就可以通过 MEM→EX 前递了。

**性能影响**：bubble-sort 中有大量 `lw` 后紧跟依赖操作的模式，导致 380 个 load-use stall，CPI 达到 1.243。

### 3.3 分支预测

#### 3.3.1 为什么需要分支预测？

在 5 级流水线中，分支指令的结果在 EX 级才计算出来。而 IF 级必须在每个 cycle 都取指令。如果不做预测，每次遇到分支就得等 2 cycle（fetch penalty = EX 到 IF 的距离），这对性能是灾难性的。

#### 3.3.2 2-bit 饱和计数器（BHT）

```
状态转移图：

  Not Taken         Taken
  ◁──────┐   ┌──────▷
         │   │
  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐
  │ SNT │◁│ WNT │◁│ WT  │◁│ ST  │
  │ (0) │▷│ (1) │▷│ (2) │▷│ (3) │
  └─────┘ └─────┘ └─────┘ └─────┘

  计数器 >= 2 → 预测 Taken
  计数器 <  2 → 预测 Not Taken
```

为什么是 2-bit 而不是 1-bit？考虑一个循环 100 次：
- 1-bit：循环出口翻转状态，进入下一轮循环第一次又预测错 → **2 次错/循环**
- 2-bit：出口从 ST→WT，仍然预测 Taken → **1 次错/循环**

#### 3.3.3 BTB（Branch Target Buffer）

BHT 只预测方向（跳/不跳），不知道目标地址。BTB 是一个小型 cache，存储 `(PC → target)` 映射。

只有 **BHT 预测 Taken 且 BTB 命中** 时才重定向 PC。否则默认 PC+4。

#### 3.3.4 预测验证与 Flush

EX 级算出实际结果后，对比 IF 级的预测：

```c
if (actual_taken != predicted_taken || actual_target != predicted_target) {
    // 误预测！冲刷 IF 和 ID 中的错误指令
    pipe.flush_if = true;
    pipe.flush_id = true;
    pipe.pc_next = correct_target;
    perf.flush_branch++;   // 代价 = 2 cycles (两条错误指令被清除)
}
```

**性能影响**：crc32 程序分支模式不规则，预测准确率仅 71.19%，大量 misprediction 导致 CPI 达到 1.192。

### 3.4 Cache 模型

#### 3.4.1 真实数据存储

每个 cache line 包含一个真实的 `data[64]` 数组：

```c
typedef struct {
    bool     valid;          // 有效位
    bool     dirty;          // 脏位（被写过但未回写主存）
    uint32_t tag;            // 标签（地址高位）
    uint32_t lru_counter;    // LRU 计数器
    uint8_t  data[64];       // ← 真实存储 64 字节数据！
} CacheLine;
```

#### 3.4.2 地址分解

以 64 组 × 4 路 × 64B 块 = 16KB cache 为例：

```
地址: 0x80001234
      ┌──────────────┬──────────┬──────────┐
      │     tag       │  index   │  offset  │
      │  addr[31:12]  │ addr[11:6] │ addr[5:0] │
      │   = 0x80001   │  = 0x08  │  = 0x34  │
      └──────────────┴──────────┴──────────┘
           选组内匹配      选组     块内偏移
```

#### 3.4.3 写策略：Write-Back + Write-Allocate

- **Write-Back**：写操作只修改 cache line 中的数据，标记 `dirty=true`。只有在该行被替换时才写回 pmem。减少了写主存的次数。
- **Write-Allocate**：写 miss 时先把整个 block 从 pmem 调入 cache，然后在 cache 中修改。保证 cache line 数据的完整性。

#### 3.4.4 替换策略：LRU

命中时将该路的 `lru_counter` 置 0，其余路 +1。替换时选 counter 最大的路（最久未使用）。

#### 3.4.5 分离 I-cache 和 D-cache

| | I-cache | D-cache |
|---|---------|---------|
| 用途 | IF 级取指令 | MEM 级读写数据 |
| 配置 | 64 组 × 4 路 × 64B = 16KB | 同 |
| 脏位 | 永远不脏（只读） | 需要跟踪 |
| 接口 | `cache_fetch_inst()` | `cache_read_data()` / `cache_write_data()` |

分离的好处：IF 和 MEM 可以同时访问各自的 cache 而不冲突，这在真实处理器中叫做 **Harvard 架构**。

### 3.5 系统指令的序列化处理

`ecall`、`ebreak`、`mret`、CSR 读写等系统指令会修改控制流和特权状态，不能在推测路径上执行。处理方式：

1. IF 取到 OP_SYSTEM 时，立即标记
2. 排空管线（`drain_pipeline()`）：让已经在管线中的指令正常流完
3. 管线为空后，在管线外单独执行该系统指令
4. 更新 `pc_next`，继续正常流水

这和真实处理器中的 **serializing instruction** 概念一致。

---

## 四、性能分析

### 4.1 测试结果汇总

所有 25 个 cpu-test 均通过（HIT GOOD TRAP）。代表性测试的性能数据：

| 测试程序 | 指令数 | 周期数 | IPC | CPI | Load-use stall | Branch 准确率 | I-miss | D-miss |
|---------|--------|--------|-----|-----|---------------|-------------|--------|--------|
| bubble-sort | 2798 | 3478 | 0.804 | 1.243 | 380 | 85.92% | 6 | 3 |
| quick-sort | 3173 | 3414 | 0.929 | 1.076 | 31 | 88.72% | 15 | 5 |
| matrix-mul | 8961 | 9629 | 0.931 | 1.075 | 0 | 80.99% | 6 | 27 |
| recursion | 4545 | 5153 | 0.882 | 1.134 | 408 | 87.61% | 10 | 5 |
| crc32 | 13277 | 15820 | 0.839 | 1.192 | 1 | 71.19% | 5 | 20 |

### 4.2 CPI 分解

理想 CPI = 1.0（每条指令 1 cycle），实际 CPI > 1 的原因：

$$\text{CPI} = 1 + \text{CPI}_{\text{load-use}} + \text{CPI}_{\text{branch}} + \text{CPI}_{\text{cache}}$$

以 bubble-sort 为例：
- base = 1.0
- load-use: 380 / 2798 = +0.136
- branch mispredict: 148 × 2 / 2798 = +0.106
- cache miss: 80 / 2798 = +0.029
- **total ≈ 1.27**（实测 1.243）

### 4.3 程序特征与微架构的关系

- **bubble-sort**：大量 `lw → compare` 依赖链 → load-use stall 主导
- **crc32**：数据依赖的位操作 + 不规则分支 → branch mispredict 主导
- **matrix-mul**：循环结构规则，无 load-use（编译器安排好了），但矩阵访问有局部性 miss → D-cache miss 主导
- **quick-sort**：递归 + 随机访问 → 各项开销较均衡

---

## 五、调试心得

### 5.1 Bug #1：forward_value 读到了当前指令自己的值

**现象**：`addi sp, sp, -16` 的 rs1=x2 应该读 `0x80009000`，却得到 `0x80000010`（上一条 JAL 的链接地址写入 rd=x1）。

**原因**：`stage_ex()` 中，`out` 指针指向 `&pipe.ex_mem`。代码先将当前指令的字段写入 `out->pc`, `out->rd`, `out->reg_write`... 然后再调用 `forward_value()`。但 `forward_value()` 检查的是 `pipe.ex_mem`——同一个结构体！已经被覆盖为当前指令的值了。

```
// 错误的顺序:
out->rd = in->rd;           // ← 覆盖了 pipe.ex_mem.rd
out->reg_write = true;      // ← 覆盖了 pipe.ex_mem.reg_write
src1 = forward_value(rs1);  // ← 读到的 ex_mem.rd 是自己！
```

**修复**：先做 forwarding，再写 `out->` 字段。

```c
// 正确的顺序:
word_t src1 = forward_value(in->rs1, in->rs1_val);  // ← 先读旧值
word_t src2 = forward_value(in->rs2, in->rs2_val);
// 现在再安全地写 out
out->rd = in->rd;
out->reg_write = in->reg_write;
```

**教训**：在软件模拟硬件时，**读写顺序极其重要**。真实硬件中组合逻辑是"同时"发生的，软件中必须精心安排顺序，保证先读后写。

### 5.2 Bug #2：saved_mem_wb 快照的必要性

**现象**：MEM→EX 前递总是拿到错误的值。

**原因**：`pipeline_cycle()` 中：
1. `stage_wb()` — 消费 `mem_wb`（指令 D）
2. `stage_mem()` — **覆写** `mem_wb`（现在是指令 C 的结果）
3. `stage_ex()` — 调用 `forward_value()` 读 `mem_wb`

步骤 3 想要指令 D 的结果（两条指令之前的），但 `mem_wb` 已被步骤 2 覆盖为指令 C 的结果。

**修复**：在步骤 2 之前保存快照：

```c
stage_wb();
saved_mem_wb = pipe.mem_wb;   // ← 快照！
stage_mem();                  // 覆盖 mem_wb
stage_ex();                   // 使用 saved_mem_wb
```

**教训**：逆序模拟流水线时，每个 stage 的输出会覆盖下一级的输入。需要仔细分析哪些数据在被覆盖前需要保存。可以用**双缓冲**（double-buffered latches）彻底解决，但快照方案更轻量。

### 5.3 Bug #3：nemu-main.c 拦截了 .bin 文件

**现象**：运行 `./nemu-interpreter test.bin` 时输出 `TEST RESULT: total=0 passed=0 failed=0`，完全没进入正常 NEMU 流程。

**原因**：`nemu-main.c` 中为表达式测试添加的代码会尝试 `fopen(argv[1], "r")`，二进制文件也能打开成功，于是进入了表达式解析模式。

**修复**：检查文件扩展名，只有 `.txt` 才进入表达式测试模式。

**教训**：修改入口代码时要注意对已有功能的影响。用文件内容特征而非"能否打开"来判断模式。

### 5.4 调试方法论

整个调试过程中最有效的方法是**逐指令 trace**：

1. 在 WB 级打印每条退休指令的 `pc`, `rd`, `result`
2. 在 EX 级打印 `pc`, `inst`, `rs1_val`(寄存器), `src1`(forwarded), `rs2_val`, `src2`
3. 在 `forward_value()` 中打印匹配了哪条路径

通过对比汇编 disassembly 和 trace 输出，可以精确定位到出错的那一条指令、那一个 cycle。

---

## 六、收获的理解

### 6.1 理论与实践的差距

教科书上的流水线图看起来简洁，但实现时有大量细节：
- 立即数的编码/解码（I/S/B/U/J 五种格式，符号扩展的位拼接）
- forwarding 需要同时处理 ALU 指令和 load 指令，优先级不同
- 系统指令不能推测执行，需要序列化
- cache 的 MMIO 区域需要绕过
- 流水线的初始填充和最终排空

### 6.2 "周期精确"的含义

"周期精确"不仅是计数，更是**因果关系的精确建模**：
- 指令 A 的结果在第 N 周期的 EX 级产生
- 指令 B 在第 N 周期的 EX 级通过 forwarding 使用它
- 如果 A 是 load，B 必须等 1 cycle（load-use hazard）

每一个"等"或"不等"都精确对应真实硬件的行为。

### 6.3 性能直觉的建立

通过实际运行不同程序，建立了对以下问题的直觉：
- **为什么 bubble-sort 慢？** → 大量 load 后紧跟依赖操作
- **为什么 matrix-mul 的 IPC 高？** → 循环规则，编译器可以良好调度
- **为什么 crc32 的 CPI 高？** → 分支模式不规则，BHT 学不会
- **cache 大小怎么影响性能？** → 工作集比 cache 大时 miss rate 急剧上升
- **分支预测准确率怎么影响 CPI？** → 每次 mispredict = 2 cycle penalty

### 6.4 软件模拟硬件的核心困难

硬件是真正并行的——所有 stage 在一个时钟沿同时读输入、算结果、写输出。软件是串行的，必须用**逆序执行 + 快照**来近似这种并行语义。这个"逆序"不是随意选择的，每一步的执行顺序都有其逻辑原因，弄错顺序就会引入"跨周期"的错误行为。

---

## 七、文件索引

| 文件 | 行数 | 职责 |
|------|------|------|
| `nemu/include/cpu/pipeline.h` | ~140 | 级间锁存器结构、ALU/BR 操作码枚举、PerfCounters |
| `nemu/include/cpu/cache.h` | ~95 | CacheLine（含 data[]）、Cache 结构、配置宏 |
| `nemu/include/cpu/bpred.h` | ~125 | BHT/BTB 结构、BPrediction 结果、2-bit 状态枚举 |
| `nemu/src/cpu/pipeline.c` | ~770 | 五级流水线全部实现 + cpu_exec_pipeline() 入口 |
| `nemu/src/cpu/cache.c` | ~250 | fill_block / writeback_block / LRU / read / write |
| `nemu/src/cpu/bpred.c` | ~130 | 2-bit 饱和计数器 + BTB 查询更新 |
| `nemu/src/cpu/filelist.mk` | ~10 | 条件编译控制 |

构建与运行：
```bash
cd nemu
make -j$(nproc)
echo "pl" | ./build/riscv32-nemu-interpreter <image.bin>
```
