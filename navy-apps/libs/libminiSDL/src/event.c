#include <NDL.h>
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define EVENT_QUEUE_SIZE 64

// Build a keyname table that matches enum SDL_Keys order.
#define STR_ITEM(k) #k,
static const char *const keynames[] = {
  "NONE",
  _KEYS(STR_ITEM)
};
enum { KEY_COUNT = (int)(sizeof(keynames) / sizeof(keynames[0])) };

static SDL_Event event_queue[EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static uint8_t key_states[KEY_COUNT] = {0};

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
  if (!name || !*name) return -1;

  // strip optional "KEY_" prefix
  if (strncmp(name, "KEY_", 4) == 0) name += 4;

  // normalize to upper-case
  char norm[32];
  size_t n = 0;
  for (; name[n] && n < sizeof(norm) - 1; n++) {
    char c = name[n];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    norm[n] = c;
  }
  norm[n] = '\0';

  // common aliases
  if (strcmp(norm, "ENTER") == 0) strcpy(norm, "RETURN");
  if (strcmp(norm, "ESC") == 0) strcpy(norm, "ESCAPE");
  if (strcmp(norm, "PGUP") == 0) strcpy(norm, "PAGEUP");
  if (strcmp(norm, "PGDN") == 0) strcpy(norm, "PAGEDOWN");

  for (int i = 0; i < KEY_COUNT; i++) {
    if (strcmp(norm, keynames[i]) == 0) return i;
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
    if (sym < 0 || sym >= KEY_COUNT) {
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
    ev.key.keysym.sym = (uint8_t)sym;
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
  /* avoid usleep() (may be missing at link time) by doing a short tick spin */
  while (1) {
    if (SDL_PollEvent(event)) return 1;
    pump_events();
    uint32_t start = NDL_GetTicks();
    while ((NDL_GetTicks() - start) < 1) {
      /* busy-wait ~1ms */
    }
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
  if (numkeys) *numkeys = KEY_COUNT;
  return key_states;
}
