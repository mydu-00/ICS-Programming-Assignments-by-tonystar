#include <stdio.h>
#include <sys/time.h>

static long long usec_diff(const struct timeval *a, const struct timeval *b) {
  return (a->tv_sec - b->tv_sec) * 1000000LL + (a->tv_usec - b->tv_usec);
}

int main(void) {
  const long long interval = 500000;   // 0.5 s
  struct timeval ref, now;

  if (gettimeofday(&ref, NULL) != 0) return 1;

  for (int i = 1; i <= 6; i++) {
    do {
      gettimeofday(&now, NULL);
    } while (usec_diff(&now, &ref) < i * interval);

    printf("[timer-test] tick %d at %ld.%06ld\n",
           i, (long)now.tv_sec, (long)now.tv_usec);
  }
  return 0;
}