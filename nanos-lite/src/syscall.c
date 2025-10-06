#include <common.h>
#include "/home/tony/codingproj/ics2025/abstract-machine/am/include/am.h"
#include "syscall.h"
#include <stdint.h>
#include <stdio.h>

extern int mm_brk(uintptr_t brk);  // 新增声明

//开关strace功能在这里，要关闭就注释掉
#define CONFIG_STRACE 1

#ifdef CONFIG_STRACE
  #define STRACE_PRINT(...) printf(__VA_ARGS__)
  static const char *sys_name(uintptr_t id) {
    switch (id) {
      case SYS_exit:  return "exit";
      case SYS_yield: return "yield";
      case SYS_write: return "write";
      case SYS_brk:  return "brk";
      default:        return "unknown";
    }
  }
#endif

void do_syscall(Context *c) {
  uintptr_t id   = c->GPR1;
  uintptr_t arg0 = c->GPR2;
  uintptr_t arg1 = c->GPR3;
  uintptr_t arg2 = c->GPR4;

#ifdef CONFIG_STRACE
  const char *name = sys_name(id);
  if (id == SYS_exit) {
    STRACE_PRINT("[strace] %s(%d)", name, (int)arg0);
  } else if (id == SYS_yield) {
    STRACE_PRINT("[strace] %s()", name);
  } else {
    STRACE_PRINT("[strace] %s(%u,%u,%u)", name,
                 (unsigned)arg0, (unsigned)arg1, (unsigned)arg2);
  }
#endif

  switch (id) {
    case SYS_yield:
      c->GPRx = 0;
      break;

    case SYS_write: {
      int fd = (int)arg0;
      const char *buf = (const char *)arg1;
      size_t len = (size_t)arg2;
      if ((fd == 1 || fd == 2) && buf) {
        for (size_t i = 0; i < len; i++) putch(buf[i]);
        c->GPRx = len;
      } else {
        c->GPRx = -1;
      }
      break;
    }

    case SYS_brk: {
      int ret = mm_brk(arg0);
      c->GPRx = ret;
      break;
    }

    case SYS_exit:
#ifdef CONFIG_STRACE
      STRACE_PRINT(" = ? <halt>\n");
#endif
      halt((int)arg0);
      break;

    default:
#ifdef CONFIG_STRACE
      STRACE_PRINT(" = ? <panic>\n");
#endif
      panic("Unhandled syscall ID = %u", (unsigned)id);
  }

#ifdef CONFIG_STRACE
  if (id != SYS_exit) {
    if (id == SYS_write) {
      STRACE_PRINT(" = %d\n", (int)c->GPRx);
    } else {
      STRACE_PRINT(" = %d\n", (int)c->GPRx);
    }
  }
#endif
}
