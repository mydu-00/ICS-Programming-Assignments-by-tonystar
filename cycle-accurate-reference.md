# ICS-2025 Cycle-Accurate Pipeline Extension (Execute-in-Pipeline)

## Files
- `nemu/include/cpu/pipeline.h` - Pipeline latch structures, perf counters
- `nemu/include/cpu/cache.h` - Cache with real data storage (CacheLine.data[])
- `nemu/include/cpu/bpred.h` - Branch predictor (2-bit BHT + BTB)
- `nemu/src/cpu/pipeline.c` - 5-stage execute-in-pipeline engine (NOT functional-first)
- `nemu/src/cpu/cache.c` - Cache: fill_block/writeback_block from/to pmem
- `nemu/src/cpu/bpred.c` - Branch predictor implementation
- `nemu/src/cpu/filelist.mk` - Conditional build (blacklist when !CONFIG_CYCLE_ACCURATE)
- `nemu/src/nemu-main.c` - Modified: expression-test only triggers for .txt files

## Build
- Kconfig: `CONFIG_CYCLE_ACCURATE` in Testing & Debugging menu
- Monitor command: `pl [N]` to run in pipeline mode
- git branch: `extra`

## Architecture (Execute-in-Pipeline, rewritten from functional-first)
- Pipeline IS the execution engine — no isa_exec_once() dependency
- IF reads real instruction bytes from I-cache (cache stores actual data from pmem)
- MEM reads/writes real data via D-cache
- WB is the ONLY stage that writes cpu.gpr[] (speculative safety)
- System instructions (ecall/ebreak/mret/CSR) serialized: drain pipeline, execute in-place
- Data forwarding: EX→EX and MEM→EX (using saved_mem_wb snapshot)
- Load-use hazard detection with 1-cycle stall
- Branch prediction: 2-bit saturating counters + BTB
- Cache: L1-I/L1-D (64×4×64B=16KB each), LRU, write-back+write-allocate

## Critical Bug Fixes
1. forward_value must run BEFORE stage_ex writes to pipe.ex_mem (they share the same struct)
2. saved_mem_wb needed because stage_mem overwrites mem_wb before stage_ex reads it
3. nemu-main.c: only enter expression-test mode for .txt files (was intercepting .bin files)

## Test Results
- All 25 cpu-tests pass (HIT GOOD TRAP)
- Realistic IPC: 0.80-0.93 depending on workload
- bubble-sort: IPC=0.804 (load-use heavy), crc32: IPC=0.839 (branch-heavy)
