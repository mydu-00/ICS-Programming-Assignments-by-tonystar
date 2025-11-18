#include <nterm.h>
#include <stdarg.h>
#include <unistd.h>
#include <SDL.h>
#include <string.h>
#include <errno.h>

char handle_key(SDL_Event *ev);

static void sh_printf(const char *format, ...) {
  static char buf[256] = {};
  va_list ap;
  va_start(ap, format);
  int len = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
  term->write(buf, len);
}

static void sh_banner() {
  sh_printf("Built-in Shell in NTerm (NJU Terminal)\n\n");
}

static void sh_prompt() {
  sh_printf("sh> ");
}

static int build_argv(char *line, char *argv[], int max_arg) {
  int argc = 0;
  char *p = line;
  while (*p && argc < max_arg - 1) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') break;
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    if (*p) { *p++ = '\0'; }
  }
  argv[argc] = NULL;
  return argc;
}

static void try_exec(char *argv[]) {
  if (!argv[0]) return;
  execvp(argv[0], argv);    // 尝试按 PATH 查找
  // 失败则尝试 busybox multi-call
  if (errno == ENOENT) {
    // 构造 busybox 调用：busybox <applet> args...
    static char *bb_argv[32];
    int i = 0;
    bb_argv[i++] = (char *)"busybox";
    for (int j = 0; argv[j] && i < 31; j++) bb_argv[i++] = argv[j];
    bb_argv[i] = NULL;
    execvp("busybox", bb_argv);
  }
  sh_printf("Exec failed: %s (errno=%d)\n", argv[0], errno);
}

static void sh_handle_cmd(const char *cmd) {
  if (!cmd) return;
  char buf[256];
  strncpy(buf, cmd, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  // 去掉行尾换行和空白
  char *end = buf + strlen(buf);
  while (end > buf && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
    *--end = '\0';

  // 跳过前导空白
  char *start = buf;
  while (*start == ' ' || *start == '\t') start++;
  if (*start == '\0') return;

  // 解析 argv
  char *argv[32];
  int argc = build_argv(start, argv, 32);
  if (argc == 0) return;

  try_exec(argv);
}

void builtin_sh_run() {
  // PATH: 同时包含 /bin 与 /usr/bin
  if (setenv("PATH", "/bin:/usr/bin", 1) != 0) {
    sh_printf("Failed to set PATH\n");
  }

  sh_banner();
  sh_prompt();

  while (1) {
    SDL_Event ev;
    if (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_KEYUP || ev.type == SDL_KEYDOWN) {
        const char *res = term->keypress(handle_key(&ev));
        if (res) {
          sh_handle_cmd(res);
          sh_prompt();
        }
      }
    }
    refresh_terminal();
  }
}
