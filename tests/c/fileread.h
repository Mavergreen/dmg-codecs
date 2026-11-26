/* platform: host-agnostic -- a udif_reader over a file, for tests */
#ifndef DC_FILEREAD_H
#define DC_FILEREAD_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "udif.h"
static int fr_pread(void *ctx, void *buf, size_t len, uint64_t off) {
  ssize_t n = pread(*(int *)ctx, buf, len, (off_t)off);
  return n == (ssize_t)len ? 0 : -EIO;
}
static int fr_open(const char *dir, const char *name, int *fd, udif_reader *r) {
  char p[1024]; struct stat st;
  snprintf(p, sizeof p, "%s/%s", dir, name);
  if ((*fd = open(p, O_RDONLY)) < 0 || fstat(*fd, &st)) { perror(p); return -1; }
  r->pread = fr_pread; r->size = (uint64_t)st.st_size; r->ctx = fd;
  return 0;
}
#endif
