#include <am.h>
#include <nemu.h>

#define SYNC_ADDR (VGACTL_ADDR + 4)

void __am_gpu_config(AM_GPU_CONFIG_T *cfg);

void __am_gpu_init() {
}

void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  uint32_t vga_ctl = inl(VGACTL_ADDR);
  int width = vga_ctl >> 16;
  int height = vga_ctl & 0xffff;
  *cfg = (AM_GPU_CONFIG_T) {
    .present = true, .has_accel = false,
    .width = width, .height = height,
    .vmemsz = width * height * sizeof(uint32_t)
  };
}

void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  if (ctl->pixels) {
    int x = ctl->x, y = ctl->y, w = ctl->w, h = ctl->h;
    int i, j;
    uint32_t *fb = (uint32_t *)(uintptr_t)FB_ADDR;
    uint32_t *pixels = (uint32_t *)ctl->pixels;
    AM_GPU_CONFIG_T cfg;
    __am_gpu_config(&cfg);
    int screen_w = cfg.width, screen_h = cfg.height;
    for (j = 0; j < h; j++) {
      if (y + j >= screen_h) break;
      for (i = 0; i < w; i++) {
        if (x + i >= screen_w) break;
        int fb_idx = (y + j) * screen_w + (x + i);
        fb[fb_idx] = pixels[j * w + i];
      }
    }
  }
  if (ctl->sync) {
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}
