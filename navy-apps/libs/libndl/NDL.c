#include <stdint.h>
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

  if (evtdev < 0) {
    evtdev = open("/dev/events", O_RDONLY);
    if (evtdev < 0) return 0;
  }

  int n = read(evtdev, buf, len - 1);
  if (n <= 0) return 0;
  buf[n] = '\0';
  return 1;
}

void NDL_OpenCanvas(int *w, int *h) {
  if (getenv("NWM_APP")) {
    int fbctl = 4;
    fbdev = 5;
    screen_w = *w; screen_h = *h;
    char buf[64];
    int len = sprintf(buf, "%d %d", screen_w, screen_h);
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
  }
}

void NDL_DrawRect(uint32_t *pixels, int x, int y, int w, int h) {
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
  if (getenv("NWM_APP")) {
    evtdev = 3;
  } else if (evtdev < 0) {
    evtdev = open("/dev/events", O_RDONLY);
  }
  return 0;
}

void NDL_Quit() {
  if (evtdev >= 0 && !getenv("NWM_APP")) {
    close(evtdev);
  }
  evtdev = -1;
  ndl_inited = 0;
}
