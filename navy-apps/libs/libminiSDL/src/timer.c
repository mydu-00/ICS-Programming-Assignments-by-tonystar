#include <NDL.h>
#include <sdl-timer.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <errno.h>

typedef struct SDL_Timer {
  atomic_bool active;
  atomic_bool removed;
  atomic_bool self_free;
  uint32_t interval_ms;
  SDL_NewTimerCallback callback;
  void *param;
  pthread_t thread;
} SDL_Timer;

static void sleep_ms(uint32_t ms) {
  if (ms == 0) return;
  struct timespec ts = {
    .tv_sec = (time_t)(ms / 1000),
    .tv_nsec = (long)(ms % 1000) * 1000000L
  };
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
  }
}

static void *timer_thread(void *arg) {
  SDL_Timer *timer = (SDL_Timer *)arg;
  uint32_t delay = timer->interval_ms;

  while (atomic_load_explicit(&timer->active, memory_order_acquire)) {
    sleep_ms(delay);
    if (!atomic_load_explicit(&timer->active, memory_order_acquire)) break;

    uint32_t next = timer->callback(timer->param);
    if (next == 0) {
      atomic_store_explicit(&timer->active, false, memory_order_release);
      break;
    }
    delay = next;
  }

  if (atomic_load_explicit(&timer->self_free, memory_order_acquire)) {
    free(timer);
  }
  return NULL;
}

SDL_TimerID SDL_AddTimer(uint32_t interval, SDL_NewTimerCallback callback, void *param) {
  if (callback == NULL) return NULL;

  SDL_Timer *timer = (SDL_Timer *)malloc(sizeof(SDL_Timer));
  if (!timer) return NULL;

  if (interval == 0) interval = 1;

  atomic_init(&timer->active, true);
  atomic_init(&timer->removed, false);
  atomic_init(&timer->self_free, false);
  timer->interval_ms = interval;
  timer->callback = callback;
  timer->param = param;

  if (pthread_create(&timer->thread, NULL, timer_thread, timer) != 0) {
    free(timer);
    fprintf(stderr, "[miniSDL] SDL_AddTimer(): failed to create timer thread\n");
    return NULL;
  }

  return (SDL_TimerID)timer;
}

int SDL_RemoveTimer(SDL_TimerID id) {
  SDL_Timer *timer = (SDL_Timer *)id;
  if (!timer) return 0;

  bool expected = false;
  if (atomic_compare_exchange_strong_explicit(&timer->removed, &expected, true,
                                              memory_order_acq_rel, memory_order_acquire) == false) {
    return 0;
  }

  atomic_store_explicit(&timer->active, false, memory_order_release);

  if (pthread_equal(pthread_self(), timer->thread)) {
    atomic_store_explicit(&timer->self_free, true, memory_order_release);
    pthread_detach(timer->thread);
    return 1;
  }

  int rc = pthread_join(timer->thread, NULL);
  if (rc != 0) {
    fprintf(stderr, "[miniSDL] SDL_RemoveTimer(): pthread_join failed (%d)\n", rc);
    return 0;
  }

  if (!atomic_load_explicit(&timer->self_free, memory_order_acquire)) {
    free(timer);
  }
  return 1;
}

uint32_t SDL_GetTicks() {
  return NDL_GetTicks();
}

void SDL_Delay(uint32_t ms) {
  usleep((useconds_t)ms * 1000);
}
