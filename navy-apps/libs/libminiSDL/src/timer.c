#include <NDL.h>
#include <sdl-timer.h>
#include <stdint.h>
#include <stdio.h>

static int warned_addtimer = 0;
static int warned_rmtimer = 0;

SDL_TimerID SDL_AddTimer(uint32_t interval, SDL_NewTimerCallback callback, void *param) {
  (void)interval; (void)callback; (void)param;
  if (!warned_addtimer) {
    fprintf(stderr, "[miniSDL] SDL_AddTimer() not implemented; timer request ignored\n");
    warned_addtimer = 1;
  }
  return (SDL_TimerID)0;
}

int SDL_RemoveTimer(SDL_TimerID id) {
  (void)id;
  if (!warned_rmtimer) {
    fprintf(stderr, "[miniSDL] SDL_RemoveTimer() not implemented\n");
    warned_rmtimer = 1;
  }
  return 0;
}

uint32_t SDL_GetTicks() {
  // SDL’s requirement: ms since SDL_Init(); NDL_GetTicks() already does that.
  return NDL_GetTicks();
}

void SDL_Delay(uint32_t ms) {
  uint32_t start = NDL_GetTicks();
  while ((NDL_GetTicks() - start) < ms) {
    // busy-wait
  }
}
