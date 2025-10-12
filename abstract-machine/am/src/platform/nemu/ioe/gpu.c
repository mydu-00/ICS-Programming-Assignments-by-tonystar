#include <am.h>
#include <nemu.h>
#include <string.h>
#include <stdint.h>

#define SYNC_ADDR (VGACTL_ADDR + 4)

void __am_gpu_init() {
  // 可选：清屏
  uint32_t vga_ctl = inl(VGACTL_ADDR);
  int W = vga_ctl >> 16, H = vga_ctl & 0xffff;
  uint32_t *fb = (uint32_t *)(uintptr_t)FB_ADDR;
  for (int i = 0; i < W * H; i++) fb[i] = 0x00000000;
  outl(SYNC_ADDR, 1);
}

void __am_gpu_config(AM_GPU_CONFIG_T *cfg) {
  uint32_t vga_ctl = inl(VGACTL_ADDR);
  int width = vga_ctl >> 16;
  int height = vga_ctl & 0xffff;
  *cfg = (AM_GPU_CONFIG_T) {
    .present   = 1,
    .has_accel = 0,
    .width     = width,
    .height    = height,
    .vmemsz    = (uint32_t)width * height * 4,
  };
}

void __am_gpu_fbdraw(AM_GPU_FBDRAW_T *ctl) {
  if (ctl->pixels) {
    uint32_t vga_ctl = inl(VGACTL_ADDR);
    int W = vga_ctl >> 16;
    // 按行拷贝到显存
    for (int row = 0; row < ctl->h; row++) {
      uintptr_t dst = (uintptr_t)FB_ADDR +
        ((uintptr_t)(ctl->y + row) * W + ctl->x) * 4;
      const void *src = (const uint8_t *)ctl->pixels + (size_t)row * ctl->w * 4;
      memcpy((void *)dst, src, (size_t)ctl->w * 4);
    }
  }
  if (ctl->sync) {
    outl(SYNC_ADDR, 1);
  }
}

void __am_gpu_status(AM_GPU_STATUS_T *status) {
  status->ready = true;
}
