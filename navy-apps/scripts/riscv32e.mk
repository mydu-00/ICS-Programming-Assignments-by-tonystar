include $(NAVY_HOME)/scripts/riscv/common.mk
# Use COMMON_CFLAGS from top-level to keep ISA/ABI consistent across projects
CFLAGS  += $(COMMON_CFLAGS)
LDFLAGS += -melf32lriscv
