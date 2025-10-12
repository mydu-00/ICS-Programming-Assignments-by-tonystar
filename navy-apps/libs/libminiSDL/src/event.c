#include <NDL.h>
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define keyname(k) #k,

#define EVENT_QUEUE_SIZE 64

static SDL_Event event_queue[EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static uint8_t key_states[SDLK_LAST] = {0};

static int queue_is_empty(void) {
  return queue_head == queue_tail;
}

static int queue_is_full(void) {
  return (queue_tail + 1) % EVENT_QUEUE_SIZE == queue_head;
}

static int queue_push(const SDL_Event *ev) {
  if (queue_is_full()) return 0;
  event_queue[queue_tail] = *ev;
  queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
  return 1;
}

static int queue_pop(SDL_Event *ev) {
  if (queue_is_empty()) return 0;
  if (ev) *ev = event_queue[queue_head];
  queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
  return 1;
}

static int keyname_to_sym(const char *name) {
  for (int i = 0; i < (int)(sizeof(keyname) / sizeof(keyname[0])); i++) {
    if (strcmp(name, keyname[i]) == 0) return i;
  }
  return -1;
}

static void pump_events(void) {
  char buf[64];
  while (NDL_PollEvent(buf, sizeof(buf))) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    char type[8] = {0}, key[32] = {0};
    if (sscanf(buf, "%7s %31s", type, key) != 2) {
      fprintf(stderr, "[miniSDL] Unknown event format: %s\n", buf);
      continue;
    }
    int sym = keyname_to_sym(key);
    if (sym < 0) {
      fprintf(stderr, "[miniSDL] Unknown key: %s\n", key);
      continue;
    }
    if (strcmp(type, "kd") == 0) {
      ev.type = SDL_KEYDOWN;
      ev.key.type = SDL_KEYDOWN;
      ev.key.state = SDL_PRESSED;
      key_states[sym] = 1;
    } else if (strcmp(type, "ku") == 0) {
      ev.type = SDL_KEYUP;
      ev.key.type = SDL_KEYUP;
      ev.key.state = SDL_RELEASED;
      key_states[sym] = 0;
    } else {
      fprintf(stderr, "[miniSDL] Unknown event type: %s\n", type);
      continue;
    }
    ev.key.keysym.sym = sym;
    queue_push(&ev);
  }
}

int SDL_PushEvent(SDL_Event *ev) {
  if (!ev || !queue_push(ev)) return -1;
  return 0;
}

int SDL_PollEvent(SDL_Event *ev) {
  pump_events();
  if (!queue_pop(ev)) return 0;
  return 1;
}

int SDL_WaitEvent(SDL_Event *event) {
  while (!SDL_PollEvent(event)) {
    pump_events();
    usleep(1000);
  }
  return 1;
}

int SDL_PeepEvents(SDL_Event *ev, int numevents, int action, uint32_t mask) {
  (void)mask;
  if (action == SDL_ADDEVENT) {
    int pushed = 0;
    for (int i = 0; i < numevents; i++) {
      if (!queue_push(&ev[i])) break;
      pushed++;
    }
    return pushed;
  } else if (action == SDL_GETEVENT) {
    int popped = 0;
    for (int i = 0; i < numevents; i++) {
      if (!queue_pop(&ev[i])) break;
      popped++;
    }
    return popped;
  }
  fprintf(stderr, "[miniSDL] SDL_PeepEvents() action %d not implemented\n", action);
  return -1;
}

uint8_t* SDL_GetKeyState(int *numkeys) {
  if (numkeys) *numkeys = SDLK_LAST;
  return key_states;
}
