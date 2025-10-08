#include <fs.h>
#include <string.h>

extern size_t ramdisk_read(void *buf, size_t offset, size_t len);
extern size_t ramdisk_write(const void *buf, size_t offset, size_t len);

typedef size_t (*ReadFn) (void *buf, size_t offset, size_t len);
typedef size_t (*WriteFn) (const void *buf, size_t offset, size_t len);

typedef struct {
  char *name;
  size_t size;
  size_t disk_offset;
  ReadFn read;
  WriteFn write;
} Finfo;

enum {FD_STDIN, FD_STDOUT, FD_STDERR, FD_FB};

size_t invalid_read(void *buf, size_t offset, size_t len) {
  panic("should not reach here");
  return 0;
}

size_t invalid_write(const void *buf, size_t offset, size_t len) {
  panic("should not reach here");
  return 0;
}

/* This is the information about all files in disk. */
static Finfo file_table[] __attribute__((used)) = {
  [FD_STDIN]  = {"stdin", 0, 0, invalid_read, invalid_write},
  [FD_STDOUT] = {"stdout", 0, 0, invalid_read, invalid_write},
  [FD_STDERR] = {"stderr", 0, 0, invalid_read, invalid_write},
#include "files.h"
};

enum { NR_FILES = sizeof(file_table) / sizeof(file_table[0]) };
static size_t file_offset[NR_FILES];

void init_fs() {
  memset(file_offset, 0, sizeof(file_offset));
}

int fs_open(const char *pathname, int flags, int mode) {
  (void)flags;
  (void)mode;
  for (size_t i = 0; i < NR_FILES; i++) {
    if (strcmp(file_table[i].name, pathname) == 0) {
      file_offset[i] = 0;
      return (int)i;
    }
  }
  panic("fs_open: file %s not found", pathname);
  return -1;
}

size_t fs_read(int fd, void *buf, size_t len) {
  assert(fd >= 0 && fd < (int)NR_FILES);
  if (fd == FD_STDIN || fd == FD_STDOUT || fd == FD_STDERR || len == 0) {
    return 0;
  }

  Finfo *f = &file_table[fd];
  ReadFn reader = f->read ? f->read : ramdisk_read;
  size_t offset = file_offset[fd];
  size_t req = len;

  if (reader == ramdisk_read && f->size > 0) {
    if (offset >= f->size) {
      return 0;
    }
    size_t avail = f->size - offset;
    if (req > avail) req = avail;
  }

  size_t ret = reader(buf, f->disk_offset + offset, req);
  file_offset[fd] += ret;
  return ret;
}

size_t fs_write(int fd, const void *buf, size_t len) {
  assert(fd >= 0 && fd < (int)NR_FILES);
  if (len == 0) return 0;

  if (fd == FD_STDOUT || fd == FD_STDERR) {
    const char *p = buf;
    for (size_t i = 0; i < len; i++) putch(p[i]);
    return len;
  }
  if (fd == FD_STDIN) return 0;

  Finfo *f = &file_table[fd];
  WriteFn writer = f->write ? f->write : ramdisk_write;
  size_t offset = file_offset[fd];
  size_t req = len;

  if (writer == ramdisk_write && f->size > 0) {
    if (offset >= f->size) {
      return 0;
    }
    size_t avail = f->size - offset;
    if (req > avail) req = avail;
  }

  size_t ret = writer(buf, f->disk_offset + offset, req);
  file_offset[fd] += ret;
  return ret;
}

size_t fs_lseek(int fd, size_t offset, int whence) {
  assert(fd >= 0 && fd < (int)NR_FILES);
  if (fd == FD_STDIN || fd == FD_STDOUT || fd == FD_STDERR) {
    return 0;
  }

  size_t new_off = 0;
  switch (whence) {
    case SEEK_SET: new_off = offset; break;
    case SEEK_CUR: new_off = file_offset[fd] + offset; break;
    case SEEK_END: new_off = file_table[fd].size + offset; break;
    default: panic("fs_lseek: invalid whence %d", whence);
  }

  if (file_table[fd].size > 0) {
    assert(new_off <= file_table[fd].size);
  }

  file_offset[fd] = new_off;
  return new_off;
}

int fs_close(int fd) {
  assert(fd >= 0 && fd < (int)NR_FILES);
  return 0;
}
