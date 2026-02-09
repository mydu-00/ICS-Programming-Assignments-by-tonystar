#include <common.h>
#include <am.h>
#include "syscall.h"
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <fs.h>
#include <klib-macros.h>
#include <proc.h>

extern int mm_brk(uintptr_t brk);  // 新增声明
extern void naive_uload(PCB *pcb, const char *filename);

static const char menu_prog_path[] = "/bin/nterm";

//开关strace功能在这里，要关闭就注释掉
//#define CONFIG_STRACE 1

#ifdef CONFIG_STRACE
  #include <stdbool.h>
  #define STRACE_BUFSZ 256

  static bool strace_guard = false;

  static void strace_print_impl(const char *fmt, ...) {
    if (strace_guard) return;
    strace_guard = true;

    char buf[STRACE_BUFSZ];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    for (char *p = buf; *p; p++) putch(*p);

    strace_guard = false;
  }

  #undef STRACE_PRINT
  #define STRACE_PRINT(...) strace_print_impl(__VA_ARGS__)

  static const char *sys_name(uintptr_t id) {
    switch (id) {
      case SYS_exit:  return "exit";
      case SYS_yield: return "yield";
      case SYS_write: return "write";
      case SYS_brk:   return "brk";
      case SYS_open:  return "open";
      case SYS_read:  return "read";
      case SYS_close: return "close";
      case SYS_lseek: return "lseek";
      case SYS_gettimeofday: return "gettimeofday";
      case SYS_execve: return "execve";
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
  } else if (id == SYS_write) {
    const char *fdname = fs_getname((int)arg0);
    if (fdname) {
      STRACE_PRINT("[strace] %s(\"%s\",%p,%u)", name, fdname, (void*)arg1, (unsigned)arg2);
    } else {
      STRACE_PRINT("[strace] %s(%d,%p,%u)", name, (int)arg0, (void*)arg1, (unsigned)arg2);
    }
  } else if (id == SYS_open) {
    STRACE_PRINT("[strace] %s(%p,%d,%d)", name,
                 (void *)arg0, (int)arg1, (int)arg2);
  } else if (id == SYS_read) {
    const char *fdname = fs_getname((int)arg0);
    if (fdname) {
      STRACE_PRINT("[strace] %s(\"%s\",%p,%u)", name, fdname, (void *)arg1, (unsigned)arg2);
    } else {
      STRACE_PRINT("[strace] %s(%d,%p,%u)", name, (int)arg0, (void *)arg1, (unsigned)arg2);
    }
  } else if (id == SYS_close) {
    const char *fdname = fs_getname((int)arg0);
    if (fdname) STRACE_PRINT("[strace] %s(\"%s\")", name, fdname);
    else STRACE_PRINT("[strace] %s(%d)", name, (int)arg0);
  } else if (id == SYS_lseek) {
    const char *fdname = fs_getname((int)arg0);
    if (fdname) STRACE_PRINT("[strace] %s(\"%s\",%u,%d)", name, fdname, (unsigned)arg1, (int)arg2);
    else STRACE_PRINT("[strace] %s(%d,%u,%d)", name, (int)arg0, (unsigned)arg1, (int)arg2);
  } else if (id == SYS_gettimeofday) {
    STRACE_PRINT("[strace] %s(%p,%p)", name, (void *)arg0, (void *)arg1);
  } else if (id == SYS_execve) {
    STRACE_PRINT("[strace] %s(%p,%p,%p)", name, (void *)arg0, (void *)arg1, (void *)arg2);
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
      const void *buf = (const void *)arg1;
      size_t len = (size_t)arg2;
      c->GPRx = fs_write(fd, buf, len);
      break;
    }

    case SYS_open:
      c->GPRx = fs_open((const char *)arg0, (int)arg1, (int)arg2);
      break;

    case SYS_read:
      c->GPRx = fs_read((int)arg0, (void *)arg1, (size_t)arg2);
      break;

    case SYS_close:
      c->GPRx = fs_close((int)arg0);
      break;

    case SYS_lseek:
      c->GPRx = fs_lseek((int)arg0, (size_t)arg1, (int)arg2);
      break;

    case SYS_brk: {
      int ret = mm_brk(arg0);
      c->GPRx = ret;
      break;
    }

    case SYS_exit: {
#ifdef CONFIG_STRACE
      STRACE_PRINT(" -> exec(\"%s\")", menu_prog_path);
#endif
      id   = SYS_execve;
      arg0 = (uintptr_t)menu_prog_path;
      arg1 = 0;
      arg2 = 0;
      c->GPR1 = id;
      c->GPR2 = arg0;
      c->GPR3 = arg1;
      c->GPR4 = arg2;
      goto handle_execve;
    }

    case SYS_gettimeofday: {
      AM_TIMER_UPTIME_T uptime = io_read(AM_TIMER_UPTIME);
      if ((struct timeval *)arg0) {
        struct timeval *tv = (struct timeval *)arg0;
        tv->tv_sec  = uptime.us / 1000000;
        tv->tv_usec = uptime.us % 1000000;
      }
      c->GPRx = 0;
      break;
    }

    case SYS_execve:
handle_execve:
      naive_uload(current, (const char *)arg0);
      c->GPRx = -1;
      break;

    default:
#ifdef CONFIG_STRACE
      STRACE_PRINT(" = ? <panic>\n");
#endif
      panic("Unhandled syscall ID = %u", (unsigned)id);
  }

#ifdef CONFIG_STRACE
  if (id != SYS_exit) {
      STRACE_PRINT(" = %d\n", (int)c->GPRx);
  }
#endif
}
