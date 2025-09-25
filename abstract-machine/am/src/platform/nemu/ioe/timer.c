#include <am.h>
#include <nemu.h>

void __am_timer_init() {
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  /* Read 64-bit uptime (microseconds) from NEMU RTC device at RTC_ADDR.
   * RTC provides low 32 bits at RTC_ADDR and high 32 bits at RTC_ADDR+4.
   * Use a loop to avoid a race where low wraps between reads.
   */
  uint32_t lo, hi, hi2;
  do {
    hi  = inl((uintptr_t)RTC_ADDR + 4);
    lo  = inl((uintptr_t)RTC_ADDR);
    hi2 = inl((uintptr_t)RTC_ADDR + 4);
  } while (hi != hi2);
  uptime->us = ((uint64_t)hi << 32) | lo;
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 0;
  rtc->month  = 0;
  rtc->year   = 1900;
}
