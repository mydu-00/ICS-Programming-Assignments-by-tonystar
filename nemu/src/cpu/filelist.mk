# Cycle-accurate model: blacklist when not enabled
# (these files are in src/cpu/ which is always in DIRS-y,
#  so we must explicitly exclude them when not configured)
ifneq ($(CONFIG_CYCLE_ACCURATE),y)
SRCS-BLACKLIST-y += src/cpu/pipeline.c
SRCS-BLACKLIST-y += src/cpu/cache.c
SRCS-BLACKLIST-y += src/cpu/bpred.c
endif
