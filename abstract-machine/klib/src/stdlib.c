#include <am.h>
#include <klib.h>
#include <klib-macros.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
static unsigned long int next = 1;

int rand(void) {
  // RAND_MAX assumed to be 32767
  next = next * 1103515245 + 12345;
  return (unsigned int)(next/65536) % 32768;
}

void srand(unsigned int seed) {
  next = seed;
}

int abs(int x) {
  return (x < 0 ? -x : x);
}

int atoi(const char* nptr) {
  int x = 0;
  while (*nptr == ' ') { nptr ++; }
  while (*nptr >= '0' && *nptr <= '9') {
    x = x * 10 + *nptr - '0';
    nptr ++;
  }
  return x;
}

/* Very small bump allocator for AM/klib usage.
   - Simple, non-freeing allocator: malloc advances a pointer in a static heap.
   - free() is a no-op.
   - This is sufficient for many simple tests in the teaching environment.
   If you need a more complete allocator, replace this with one suited for your OS.
*/
#define KLIB_HEAP_SIZE (64 * 1024)
static unsigned char klib_heap[KLIB_HEAP_SIZE];
static size_t klib_heap_pos = 0;

void *malloc(size_t size) {
  if (size == 0) return NULL;
  /* align to 8 bytes */
  size_t align = 8;
  size_t cur = (klib_heap_pos + (align - 1)) & ~(align - 1);
  if (cur + size > KLIB_HEAP_SIZE) {
    /* out of memory in this simple allocator */
    return NULL;
  }
  void *ptr = &klib_heap[cur];
  klib_heap_pos = cur + size;
  return ptr;
}

void free(void *ptr) {
  /* no-op for bump allocator */
  (void)ptr;
}

#endif
