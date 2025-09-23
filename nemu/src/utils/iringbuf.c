#include <stdio.h>
#include <string.h>
#include <common.h>
#include "utils/iringbuf.h"

#ifdef CONFIG_ITRACE

#define ITRACE_RINGBUF_SIZE 64
#define ITRACE_LINE_MAX 128

static char ringbuf[ITRACE_RINGBUF_SIZE][ITRACE_LINE_MAX];
static int ring_head = 0;   /* next write index */
static int ring_count = 0;  /* number of valid entries */

void iringbuf_init(void) {
  ring_head = 0;
  ring_count = 0;
  memset(ringbuf, 0, sizeof(ringbuf));
}

void iringbuf_push(const char *line) {
  if (!line) return;
  strncpy(ringbuf[ring_head], line, ITRACE_LINE_MAX - 1);
  ringbuf[ring_head][ITRACE_LINE_MAX - 1] = '\0';
  ring_head = (ring_head + 1) % ITRACE_RINGBUF_SIZE;
  if (ring_count < ITRACE_RINGBUF_SIZE) ring_count++;
}

void iringbuf_dump(void) {
  int start = (ring_head - ring_count + ITRACE_RINGBUF_SIZE) % ITRACE_RINGBUF_SIZE;
  for (int i = 0; i < ring_count; i++) {
    int idx = (start + i) % ITRACE_RINGBUF_SIZE;
    const char *prefix = (i == ring_count - 1) ? "--> " : "    ";
    printf("%s%s\n", prefix, ringbuf[idx]);
  }
}

#endif /* CONFIG_ITRACE */