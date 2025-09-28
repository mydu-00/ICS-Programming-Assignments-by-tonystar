/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <common.h>
#include <device/map.h>
#include <SDL2/SDL.h>

enum {
  reg_freq,
  reg_channels,
  reg_samples,
  reg_sbuf_size,
  reg_init,
  reg_count,
  nr_reg
};

static uint8_t *sbuf = NULL;
static uint32_t *audio_base = NULL;

static SDL_AudioSpec want, have;
static SDL_AudioDeviceID dev = 0;
static int sbuf_size = 0;
static int sbuf_r = 0, sbuf_w = 0; // 环形缓冲区读写指针
static int sbuf_count = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
  /* 按 AM 约定从 sbuf[0] 开始读取，读取后把剩余数据左移到开头 */
  int nread = (len < sbuf_count) ? len : sbuf_count;
  if (nread > 0) {
    memcpy(stream, sbuf, nread);
    if (sbuf_count > nread) {
      memmove(sbuf, sbuf + nread, sbuf_count - nread);
    }
    sbuf_count -= nread;
  }
  if (len > nread) {
    memset(stream + nread, 0, len - nread);
  }
}

static void audio_io_handler(uint32_t offset, int len, bool is_write) {
  uint32_t idx = offset / 4;
  if (is_write) {
    uint32_t val = *(uint32_t *)((uint8_t *)audio_base + offset);
    switch (idx) {
      case reg_freq:
        audio_base[reg_freq] = val;
        break;
      case reg_channels:
        audio_base[reg_channels] = val;
        break;
      case reg_samples:
        audio_base[reg_samples] = val;
        break;
      case reg_init:
        audio_base[reg_init] = val;
        if (audio_base[reg_init]) {
          want.freq = audio_base[reg_freq];
          want.format = AUDIO_S16SYS;
          want.channels = audio_base[reg_channels];
          want.samples = audio_base[reg_samples];
          want.callback = audio_callback;
          want.userdata = NULL;
          if (dev) SDL_CloseAudioDevice(dev);
          dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
          if (dev != 0) SDL_PauseAudioDevice(dev, 0);
        }
        break;
      case reg_count:
        /* guest 通知已写入的字节数（按 AM 约定为从 sbuf[0] 起的线性长度） */
        audio_base[reg_count] = val;
        if ((int)val <= sbuf_size) sbuf_count = (int)val;
        else sbuf_count = sbuf_size;
        break;
      default:
        break;
    }
  } else {
    switch (idx) {
      case reg_sbuf_size:
        audio_base[reg_sbuf_size] = sbuf_size;
        break;
      case reg_count:
        audio_base[reg_count] = sbuf_count;
        break;
      default:
        break;
    }
  }
}

// 播放数据写入流缓冲区
void audio_write(const uint8_t *data, int len) {
  while (len > 0) {
    while (sbuf_count == sbuf_size) SDL_Delay(1); // 等待有空间
    int space = sbuf_size - sbuf_count;
    int chunk = sbuf_w >= sbuf_r ? sbuf_size - sbuf_w : sbuf_r - sbuf_w;
    if (chunk > len) chunk = len;
    if (chunk > space) chunk = space;
    memcpy(sbuf + sbuf_w, data, chunk);
    sbuf_w = (sbuf_w + chunk) % sbuf_size;
    sbuf_count += chunk;
    data += chunk;
    len -= chunk;
  }
}

void init_audio() {
  uint32_t space_size = sizeof(uint32_t) * nr_reg;
  audio_base = (uint32_t *)new_space(space_size);
#ifdef CONFIG_HAS_PORT_IO
  add_pio_map ("audio", CONFIG_AUDIO_CTL_PORT, audio_base, space_size, audio_io_handler);
#else
  add_mmio_map("audio", CONFIG_AUDIO_CTL_MMIO, audio_base, space_size, audio_io_handler);
#endif

  sbuf_size = CONFIG_SB_SIZE;
  sbuf = (uint8_t *)new_space(sbuf_size);
  add_mmio_map("audio-sbuf", CONFIG_SB_ADDR, sbuf, sbuf_size, NULL);

  sbuf_r = sbuf_w = sbuf_count = 0;
  SDL_Init(SDL_INIT_AUDIO);
}
