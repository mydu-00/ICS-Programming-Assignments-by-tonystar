#include <common.h>
#include <am.h>
#include <string.h>

#if defined(MULTIPROGRAM) && !defined(TIME_SHARING)
# define MULTIPROGRAM_YIELD() yield()
#else
# define MULTIPROGRAM_YIELD()
#endif

#define NAME(key) \
  [AM_KEY_##key] = #key,

static const char *keyname[256] __attribute__((used)) = {
  [AM_KEY_NONE] = "NONE",
  AM_KEYS(NAME)
};

size_t serial_write(const void *buf, size_t offset, size_t len) {
  (void)offset;
  const char *p = buf;
  for (size_t i = 0; i < len; i++) {
    putch(p[i]);
  }
  return len;
}

size_t events_read(void *buf, size_t offset, size_t len) {
  (void)offset;
  if (len == 0) return 0;

  AM_INPUT_KEYBRD_T kbd = io_read(AM_INPUT_KEYBRD);
  if (kbd.keycode == AM_KEY_NONE) return 0;

  char event[32];
  const char *name = keyname[kbd.keycode];
  int n = snprintf(event, sizeof(event), "%s %s\n",
                   kbd.keydown ? "kd" : "ku", name);
  if (n <= 0) return 0;

  size_t out = (size_t)n;
  if (out > len) out = len;
  memcpy(buf, event, out);
  return out;
}

size_t dispinfo_read(void *buf, size_t offset, size_t len) {
  return 0;
}

size_t fb_write(const void *buf, size_t offset, size_t len) {
  return 0;
}

void init_device() {
  Log("Initializing devices...");
  ioe_init();
}
