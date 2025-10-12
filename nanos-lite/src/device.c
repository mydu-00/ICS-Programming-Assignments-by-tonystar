#include <common.h>
#include <am.h>
#include <stdio.h>
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

static int screen_w = 0;
static int screen_h = 0;
static char dispinfo_buf[64];
static size_t dispinfo_len = 0;

size_t serial_write(const void *buf, size_t offset, size_t len) {
  (void)offset;
  const char *p = buf;
  for (size_t i = 0; i < len; i++) {
    putch(p[i]);
  }
  return len;
}

size_t dispinfo_read(void *buf, size_t offset, size_t len) {
  if (offset >= dispinfo_len) return 0;
  size_t avail = dispinfo_len - offset;
  if (len > avail) len = avail;
  memcpy(buf, dispinfo_buf + offset, len);
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

size_t fb_write(const void *buf, size_t offset, size_t len) {
  if (len == 0) return 0;
  assert(screen_w > 0 && screen_h > 0);
  assert((offset & 3) == 0);
  assert((len & 3) == 0);

  const uint32_t *pixels = buf;
  size_t pixel_offset = offset >> 2;
  int x = pixel_offset % screen_w;
  int y = pixel_offset / screen_w;
  size_t remaining = len >> 2;
  size_t consumed = 0;

  while (remaining > 0) {
    int chunk = screen_w - x;
    if ((size_t)chunk > remaining) chunk = (int)remaining;
    io_write(AM_GPU_FBDRAW,
      .x = x,
      .y = y,
      .w = chunk,
      .h = 1,
      .pixels = (void *)(pixels + consumed),
      .sync = 0
    );
    consumed += chunk;
    remaining -= chunk;
    x = 0;
    y += 1;
  }

  io_write(AM_GPU_FBDRAW, .sync = 1);
  return len;
}

size_t fb_size(void) {
  return (size_t)screen_w * screen_h * 4;
}

void init_device() {
  Log("Initializing devices...");
  ioe_init();
  AM_GPU_CONFIG_T cfg = io_read(AM_GPU_CONFIG);
  screen_w = cfg.width;
  screen_h = cfg.height;
  dispinfo_len = snprintf(dispinfo_buf, sizeof(dispinfo_buf),
                          "WIDTH : %d\nHEIGHT: %d\n", screen_w, screen_h);
}
