#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>

static int evtdev = -1;
static int fbdev = -1;
static int screen_w = 0, screen_h = 0;
static struct timeval boot_tv = {0};
static int ndl_inited = 0;
static int canvas_w = 0, canvas_h = 0;
static int canvas_x = 0, canvas_y = 0;

static void ensure_dispinfo(void) {
  if (screen_w > 0 && screen_h > 0) return;
  int fd = open("/proc/dispinfo", O_RDONLY);
  if (fd < 0) return;
  char buf[128];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return;
  buf[n] = '\0';
  int w = 0, h = 0;
  if (sscanf(buf, "%*[^0-9]%d%*[^0-9]%d", &w, &h) == 2) {
    screen_w = w; screen_h = h;
  }
}

static void ensure_evtdev(void) {
  if (evtdev >= 0) return;
  if (getenv("NWM_APP")) return;
  evtdev = open("/dev/events", O_RDONLY);
}

static void ensure_fbdev(void) {
  if (fbdev >= 0) return;
  if (getenv("NWM_APP")) return;
  fbdev = open("/dev/fb", O_WRONLY);
}

uint32_t NDL_GetTicks() {
  struct timeval now;
  if (!ndl_inited) return 0;
  gettimeofday(&now, NULL);
  uint64_t us = (uint64_t)(now.tv_sec - boot_tv.tv_sec) * 1000000ULL +
                (uint64_t)(now.tv_usec - boot_tv.tv_usec);
  return (uint32_t)(us / 1000);
}

int NDL_PollEvent(char *buf, int len) {
  if (!ndl_inited || buf == NULL || len <= 0) return 0;

  ensure_evtdev();
  if (evtdev < 0) return 0;

  int n = read(evtdev, buf, len - 1);
  if (n <= 0) return 0;
  buf[n] = '\0';
  return 1;
}

void NDL_OpenCanvas(int *w, int *h) {
  assert(w && h);
  ensure_dispinfo();

  if (*w == 0 && *h == 0) {
    *w = screen_w;
    *h = screen_h;
  }
  assert(screen_w > 0 && screen_h > 0);
  assert(*w <= screen_w && *h <= screen_h);

  canvas_w = *w;
  canvas_h = *h;

  /* center the canvas on the screen for better visual effect */
  canvas_x = (screen_w - canvas_w) / 2;
  canvas_y = (screen_h - canvas_h) / 2;
  if (canvas_x < 0) canvas_x = 0;
  if (canvas_y < 0) canvas_y = 0;

  if (getenv("NWM_APP")) {
    int fbctl = 4;
    fbdev = 5;
    screen_w = *w;
    screen_h = *h;
    char buf[64];
    int len = sprintf(buf, "%d %d", canvas_w, canvas_h);
    // let NWM resize the window and create the frame buffer
    write(fbctl, buf, len);
    while (1) {
      // 3 = evtdev
      int nread = read(3, buf, sizeof(buf) - 1);
      if (nread <= 0) continue;
      buf[nread] = '\0';
      if (strcmp(buf, "mmap ok") == 0) break;
    }
    close(fbctl);
  } else {
    ensure_fbdev();
  }
}

void NDL_DrawRect(uint32_t *pixels, int x, int y, int w, int h) {
  if (pixels == NULL || w <= 0 || h <= 0) return;
  if (getenv("NWM_APP")) return;
  ensure_dispinfo();
  ensure_fbdev();
  assert(fbdev >= 0);
  assert(x >= 0 && y >= 0);
  assert(x + w <= canvas_w);
  assert(y + h <= canvas_h);

  for (int row = 0; row < h; row++) {
    off_t off = (off_t)(canvas_y + y + row) * screen_w + (canvas_x + x);
    off *= 4;
    lseek(fbdev, off, SEEK_SET);
    write(fbdev, pixels + row * w, (size_t)w * 4);
  }
}

void NDL_OpenAudio(int freq, int channels, int samples) {
}

void NDL_CloseAudio() {
}

int NDL_PlayAudio(void *buf, int len) {
  return 0;
}

int NDL_QueryAudio() {
  return 0;
}

int NDL_Init(uint32_t flags) {
  if (!ndl_inited) {
    gettimeofday(&boot_tv, NULL);
    ndl_inited = 1;
  }
  ensure_dispinfo();
  if (getenv("NWM_APP")) {
    evtdev = 3;
  } else {
    ensure_evtdev();
  }
  return 0;
}

void NDL_Quit() {
  if (!getenv("NWM_APP")) {
    if (evtdev >= 0) close(evtdev);
    if (fbdev >= 0) close(fbdev);
  }
  evtdev = -1;
  fbdev = -1;
  ndl_inited = 0;
}
