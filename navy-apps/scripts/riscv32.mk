include $(NAVY_HOME)/scripts/riscv/common.mk

# 如果上层传了 VME=1，则加上 HAS_VME 宏
ifeq ($(VME),1)
CFLAGS  += -DHAS_VME
endif

CFLAGS  += -march=rv32g -mabi=ilp32  # overwrite
LDFLAGS += -melf32lriscv
