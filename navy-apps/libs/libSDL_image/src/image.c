#define SDL_malloc  malloc
#define SDL_free    free
#define SDL_realloc realloc

#define SDL_STBIMAGE_IMPLEMENTATION
#include "SDL_stbimage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

SDL_Surface* IMG_Load_RW(SDL_RWops *src, int freesrc) {
  assert(src->type == RW_TYPE_MEM);
  assert(freesrc == 0);
  return NULL;
}

SDL_Surface* IMG_Load(const char *filename) {
  if (!filename) return NULL;

  SDL_Surface *surface = NULL;
  FILE *fp = fopen(filename, "rb");
  if (!fp) return NULL;

  if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
  long size = ftell(fp);
  if (size < 0) { fclose(fp); return NULL; }
  if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }

  void *buf = SDL_malloc((size_t)size);
  if (!buf) { fclose(fp); return NULL; }

  size_t n = fread(buf, 1, (size_t)size, fp);
  fclose(fp);
  if (n != (size_t)size) {
    SDL_free(buf);
    return NULL;
  }

  surface = STBIMG_LoadFromMemory((const unsigned char *)buf, (int)size);
  SDL_free(buf);
  return surface;
}

int IMG_isPNG(SDL_RWops *src) {
  return 0;
}

SDL_Surface* IMG_LoadJPG_RW(SDL_RWops *src) {
  return IMG_Load_RW(src, 0);
}

char *IMG_GetError() {
  return "Navy does not support IMG_GetError()";
}
