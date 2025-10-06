#include <common.h>
#include "/home/tony/codingproj/ics2025/abstract-machine/am/include/am.h"
#include "syscall.h"

# define CONFIG_STRACE 1

#ifdef CONFIG_STRACE
  #ifdef CONFIG_STRACE_TO_LOG
    extern void log_write(const char *fmt, ...);
    #define STRACE_PRINT(...) log_write(__VA_ARGS__)
  #else
    #define STRACE_PRINT(...) printf(__VA_ARGS__)
  #endif

  static const char *sys_name(uintptr_t id) {
    switch (id) {
      case SYS_exit:  return "exit";
      case SYS_yield: return "yield";
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
    STRACE_PRINT(" = %ld\n", (long)c->GPRx);
  }
#endif
}
