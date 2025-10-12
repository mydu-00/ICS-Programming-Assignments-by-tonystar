#include <fs.h>
#include <string.h>

extern size_t ramdisk_read(void *buf, size_t offset, size_t len);
extern size_t ramdisk_write(const void *buf, size_t offset, size_t len);
extern size_t serial_write(const void *buf, size_t offset, size_t len);
extern size_t events_read(void *buf, size_t offset, size_t len);

typedef size_t (*ReadFn)(void *buf, size_t offset, size_t len);
typedef size_t (*WriteFn)(const void *buf, size_t offset, size_t len);

typedef struct {
  const char *name;
  size_t size;
  size_t disk_offset;
  ReadFn read;
  WriteFn write;
} Finfo;

enum { FD_STDIN, FD_STDOUT, FD_STDERR };

static size_t invalid_read(void *buf, size_t offset, size_t len) {
  panic("invalid read");
  return 0;
}
static size_t invalid_write(const void *buf, size_t offset, size_t len) {
  panic("invalid write");
  return 0;
}

/* 占位 + 真正文件列表 */
static Finfo file_table[] __attribute__((used)) = {
  [FD_STDIN]  = {"stdin",  0, 0, invalid_read, invalid_write},
  [FD_STDOUT] = {"stdout", 0, 0, invalid_read, serial_write},
  [FD_STDERR] = {"stderr", 0, 0, invalid_read, serial_write},
  {"/dev/events", 0, 0, events_read, invalid_write},
#include "files.h"
};

enum { NR_FILES = sizeof(file_table) / sizeof(file_table[0]) };
static size_t file_offset[NR_FILES];

void init_fs(void) {
  memset(file_offset, 0, sizeof(file_offset));
}

const char *fs_getname(int fd) {
  if (fd < 0 || fd >= (int)NR_FILES) return NULL;
  return file_table[fd].name;
}

int fs_open(const char *pathname, int flags, int mode) {
  (void)flags; (void)mode;
  assert(pathname);
  for (int i = 0; i < NR_FILES; i++) {
    if (file_table[i].name && strcmp(file_table[i].name, pathname) == 0) {
      file_offset[i] = 0;
      return i;
    }
  }
  panic("fs_open: %s not found", pathname);
  return -1;
}

size_t fs_read(int fd, void *buf, size_t len) {
  assert(fd >= 0 && fd < NR_FILES);
  if (len == 0) return 0;
  if (fd <= FD_STDERR) return 0;

  Finfo *f = &file_table[fd];
  ReadFn r = f->read ? f->read : ramdisk_read;
  size_t off = file_offset[fd];
  if (f->size > 0) {
    if (off >= f->size) return 0;
    size_t avail = f->size - off;
    if (len > avail) len = avail;
  }
  size_t n = r(buf, f->disk_offset + off, len);
  file_offset[fd] += n;
  return n;
}

size_t fs_write(int fd, const void *buf, size_t len) {
  assert(fd >= 0 && fd < NR_FILES);
  if (len == 0) return 0;
  if (fd == FD_STDOUT || fd == FD_STDERR) {
    const char *p = buf;
    for (size_t i = 0; i < len; i++) putch(p[i]);
    return len;
  }
  if (fd == FD_STDIN) return 0;

  Finfo *f = &file_table[fd];
  WriteFn w = f->write ? f->write : ramdisk_write;
  size_t off = file_offset[fd];
  if (f->size > 0) {
    if (off >= f->size) return 0;
    size_t avail = f->size - off;
    if (len > avail) len = avail;
  }
  size_t n = w(buf, f->disk_offset + off, len);
  file_offset[fd] += n;
  return n;
}

size_t fs_lseek(int fd, size_t offset, int whence) {
  assert(fd >= 0 && fd < NR_FILES);
  if (fd <= FD_STDERR) return 0;
  size_t base;
  switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = file_offset[fd]; break;
    case SEEK_END: base = file_table[fd].size; break;
    default: panic("lseek whence");
  }
  size_t new_off = base + offset;
  if (file_table[fd].size > 0) assert(new_off <= file_table[fd].size);
  file_offset[fd] = new_off;
  return new_off;
}

int fs_close(int fd) {
  assert(fd >= 0 && fd < NR_FILES);
  return 0;
}
