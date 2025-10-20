#include <sdl-file.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  SDL_RWops base;
  uint8_t *buf;
  size_t size;
  size_t pos;
  int autofree;
  int writable;
} SDL_RWopsMem;

static SDL_RWopsMem *rw_as_mem(SDL_RWops *rw) {
  return (SDL_RWopsMem *)rw;
}

static int64_t mem_size(SDL_RWops *rw) {
  SDL_RWopsMem *m = rw_as_mem(rw);
  return (int64_t)m->size;
}

static int64_t mem_seek(SDL_RWops *rw, int64_t offset, int whence) {
  SDL_RWopsMem *m = rw_as_mem(rw);
  int64_t new_pos;

  switch (whence) {
    case RW_SEEK_SET: new_pos = offset; break;
    case RW_SEEK_CUR: new_pos = (int64_t)m->pos + offset; break;
    case RW_SEEK_END: new_pos = (int64_t)m->size + offset; break;
    default: return -1;
  }

  if (new_pos < 0 || (uint64_t)new_pos > m->size) return -1;

  m->pos = (size_t)new_pos;
  return new_pos;
}

static size_t mem_read(SDL_RWops *rw, void *buf, size_t size, size_t nmemb) {
  SDL_RWopsMem *m = rw_as_mem(rw);
  if (size == 0 || nmemb == 0) return 0;

  size_t total = size * nmemb;
  size_t remain = m->size - m->pos;
  if (total > remain) total = remain;

  memcpy(buf, m->buf + m->pos, total);
  m->pos += total;
  return total / size;
}

static size_t mem_write(SDL_RWops *rw, const void *buf, size_t size, size_t nmemb) {
  SDL_RWopsMem *m = rw_as_mem(rw);
  if (!m->writable || size == 0 || nmemb == 0) return 0;

  size_t total = size * nmemb;
  size_t remain = m->size - m->pos;
  if (total > remain) total = remain;

  memcpy(m->buf + m->pos, buf, total);
  m->pos += total;
  return total / size;
}

static int mem_close(SDL_RWops *rw) {
  SDL_RWopsMem *m = rw_as_mem(rw);
  if (m->autofree && m->buf) {
    free(m->buf);
    m->buf = NULL;
  }
  free(m);
  return 0;
}

static SDL_RWops *create_mem_rw(uint8_t *buf, size_t size, int autofree, int writable) {
  SDL_RWopsMem *m = (SDL_RWopsMem *)malloc(sizeof(SDL_RWopsMem));
  if (!m) {
    if (autofree) free(buf);
    return NULL;
  }
  m->buf = buf;
  m->size = size;
  m->pos = 0;
  m->autofree = autofree;
  m->writable = writable;

  SDL_RWops *rw = &m->base;
  rw->type = RW_TYPE_MEM;
  rw->fp = NULL;
  rw->mem.base = buf;
  rw->mem.size = (ssize_t)size;
  rw->size  = mem_size;
  rw->seek  = mem_seek;
  rw->read  = mem_read;
  rw->write = mem_write;
  rw->close = mem_close;
  return rw;
}

SDL_RWops* SDL_RWFromFile(const char *filename, const char *mode) {
  if (!filename || !mode) return NULL;
  FILE *fp = fopen(filename, mode);
  if (!fp) return NULL;

  if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
  long sz = ftell(fp);
  if (sz < 0) { fclose(fp); return NULL; }
  if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }

  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) { fclose(fp); return NULL; }

  size_t n = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  if (n != (size_t)sz) {
    free(buf);
    return NULL;
  }

  return create_mem_rw(buf, (size_t)sz, 1, 0);
}

SDL_RWops* SDL_RWFromMem(void *mem, int size) {
  if (!mem || size < 0) return NULL;
  return create_mem_rw((uint8_t *)mem, (size_t)size, 0, 1);
}

void SDL_FreeRW(SDL_RWops *rw) {
  if (!rw) return;
  rw->close(rw);
}
