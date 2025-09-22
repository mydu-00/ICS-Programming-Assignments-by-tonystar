#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

/* Minimal helper to output through AM: _putc is commonly provided by AM.
   If your AM uses a different name, replace _putc below with that function.
*/
extern void _putc(char c);

static void out_putc(char **outp, size_t *rem, int *written, char c) {
  if (outp && *outp && rem) {
    if (*rem > 0) {
      **outp = c;
      (*outp)++;
      (*rem)--;
    }
  }
  (*written)++;
}

static void out_puts(char **outp, size_t *rem, int *written, const char *s) {
  while (*s) out_putc(outp, rem, written, *s++);
}

/* convert unsigned value to string in given base (lowercase). returns pointer to buffer */
static char *u32toa(unsigned long val, unsigned int base, char *buf_end) {
  static const char digits[] = "0123456789abcdef";
  char *p = buf_end;

  /* ensure there is a terminating NUL at the end of the buffer */
  *--p = '\0';

  if (val == 0) {
    *--p = '0';
    return p;
  }
  while (val != 0) {
    *--p = digits[val % base];
    val /= base;
  }
  return p;
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  char *outp = out;
  size_t rem = (n > 0) ? n - 1 : 0; /* reserve space for NUL if n>0 */
  int written = 0;

  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      out_putc(out ? &outp : NULL, &rem, &written, *fmt);
      continue;
    }

    fmt++; /* skip '%' */
    if (*fmt == '%') {
      out_putc(out ? &outp : NULL, &rem, &written, '%');
      continue;
    }

    /* (very) minimal parsing: ignore flags/width/precision for now */
    /* support: c, s, d, i, u, x, p */
    if (*fmt == 'c') {
      int c = va_arg(ap, int);
      out_putc(out ? &outp : NULL, &rem, &written, (char)c);
      continue;
    } else if (*fmt == 's') {
      const char *s = va_arg(ap, const char *);
      if (!s) s = "(null)";
      out_puts(out ? &outp : NULL, &rem, &written, s);
      continue;
    } else if (*fmt == 'd' || *fmt == 'i') {
      int v = va_arg(ap, int);
      unsigned long uv;
      char tmp[32];
      char *s;
      if (v < 0) {
        out_putc(out ? &outp : NULL, &rem, &written, '-');
        /* careful with INT_MIN promotion */
        uv = (unsigned long)(-(long)v);
      } else {
        uv = (unsigned long)v;
      }
      s = u32toa(uv, 10, tmp + sizeof(tmp));
      out_puts(out ? &outp : NULL, &rem, &written, s);
      continue;
    } else if (*fmt == 'u') {
      unsigned int v = va_arg(ap, unsigned int);
      char tmp[32];
      char *s = u32toa((unsigned long)v, 10, tmp + sizeof(tmp));
      out_puts(out ? &outp : NULL, &rem, &written, s);
      continue;
    } else if (*fmt == 'x') {
      unsigned int v = va_arg(ap, unsigned int);
      char tmp[32];
      char *s = u32toa((unsigned long)v, 16, tmp + sizeof(tmp));
      out_puts(out ? &outp : NULL, &rem, &written, s);
      continue;
    } else if (*fmt == 'p') {
      void *ptr = va_arg(ap, void *);
      uintptr_t v = (uintptr_t)ptr;
      char tmp[2 + sizeof(uintptr_t) * 2 + 1]; /* "0x" + hex digits + NUL */
      char *bufend = tmp + sizeof(tmp);
      char *s = u32toa((unsigned long)v, 16, bufend);
      out_puts(out ? &outp : NULL, &rem, &written, "0x");
      out_puts(out ? &outp : NULL, &rem, &written, s);
      continue;
    } else {
      /* unknown specifier, print it literally */
      out_putc(out ? &outp : NULL, &rem, &written, '%');
      out_putc(out ? &outp : NULL, &rem, &written, *fmt);
      continue;
    }
  }

  /* NUL terminate if space available */
  if (n > 0 && out) {
    *outp = '\0';
  }

  return written;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  int ret;
  va_list ap;
  va_start(ap, fmt);
  ret = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return ret;
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  /* pass very large n so vsnprintf won't truncate (semantic of vsprintf) */
  return vsnprintf(out, (size_t)SIZE_MAX, fmt, ap);
}

int sprintf(char *out, const char *fmt, ...) {
  int ret;
  va_list ap;
  va_start(ap, fmt);
  /* sprintf writes into out assuming caller provided enough space */
  ret = vsnprintf(out, (size_t)SIZE_MAX, fmt, ap);
  va_end(ap);
  return ret;
}

int printf(const char *fmt, ...) {
  /* format into a fixed stack buffer then emit via AM _putc */
  char buf[1024];
  int ret;
  va_list ap;
  va_start(ap, fmt);
  ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  /* emit to console one character at a time */
  for (int i = 0; i < ret; i++) {
    _putc(buf[i]);
  }
  return ret;
}

#endif
