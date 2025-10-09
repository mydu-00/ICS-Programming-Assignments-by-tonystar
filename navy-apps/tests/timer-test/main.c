#include <stdio.h>
#include <stdint.h>
#include <NDL.h>

int main(void) {
  const uint32_t interval = 500;  // ms
  NDL_Init(0);
  uint32_t start = NDL_GetTicks();

  for (int i = 1; i <= 6; i++) {
    uint32_t target = start + i * interval;
    uint32_t now;
    do {
      now = NDL_GetTicks();
    } while ((int32_t)(target - now) > 0);

    printf("[timer-test] tick %d at %u.%03u\n",
           i, now / 1000, now % 1000);
  }

  NDL_Quit();
  return 0;
}