#include <am.h>
#include <nemu.h>

#define KEYDOWN_MASK 0x8000
#define KEY_QUEUE_LEN 1024

static int key_queue[KEY_QUEUE_LEN] = {};
static int key_f = 0, key_r = 0;

void __am_input_keybrd(AM_INPUT_KEYBRD_T *kbd) {
  // 先把 NEMU 的 i8042 事件全部读入本地队列
  while (1) {
    uint32_t data = inl(KBD_ADDR);
    if ((data & ~KEYDOWN_MASK) == 0) break; // AM_KEY_NONE
    key_queue[key_r] = data;
    key_r = (key_r + 1) % KEY_QUEUE_LEN;
    // 防止溢出
    if (key_r == key_f) key_f = (key_f + 1) % KEY_QUEUE_LEN;
  }

  uint32_t k = AM_KEY_NONE;
  if (key_f != key_r) {
    k = key_queue[key_f];
    key_f = (key_f + 1) % KEY_QUEUE_LEN;
  }
  kbd->keydown = (k & KEYDOWN_MASK) != 0;
  kbd->keycode = k & ~KEYDOWN_MASK;
}
