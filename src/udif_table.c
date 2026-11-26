#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "udif.h"

/* spec: an XML plist bigger than this is refused rather than allocated; real ones are a few MiB at most */
#define MAX_XML ((uint64_t)64 << 20)

struct udif_table {
  udif_run *runs; size_t n, cap; uint64_t sectors, dfo, dfl;
  uint32_t *mish_type, *mish_crc; size_t nmish, mishcap;
  uint32_t master_type, master_crc, fork_type, fork_crc;
};

static uint64_t be64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = v << 8 | p[i]; return v; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int add_mish(void *ctx, const uint8_t *m, size_t len) {
  udif_table *t = ctx;
  if (len < 0xcc || memcmp(m, "mish", 4)) return -EINVAL;
  uint64_t first = be64(m + 8), ds = be64(m + 0x18);
  uint32_t n = be32(m + 0xc8);
  if (n > (len - 0xcc) / 40) return -EINVAL;
  if (t->nmish == t->mishcap) {
    size_t c = t->mishcap ? t->mishcap * 2 : 16;
    uint32_t *a = realloc(t->mish_type, c * sizeof *a);
    if (!a) return -ENOMEM;
    t->mish_type = a;
    uint32_t *b = realloc(t->mish_crc, c * sizeof *b);
    if (!b) return -ENOMEM;
    t->mish_crc = b; t->mishcap = c;
  }
  uint32_t mish = (uint32_t)t->nmish++;
  t->mish_type[mish] = be32(m + 0x40); t->mish_crc[mish] = be32(m + 0x48);
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t *r = m + 0xcc + 40 * (size_t)i;
    udif_run run = { be32(r), first + be64(r + 8), be64(r + 16), be64(r + 24), be64(r + 32), mish };
    if (run.type == 0xffffffff || run.type == 0x7ffffffe || run.count == 0) continue;
    if (run.sector < first || run.sector > t->sectors || run.count > t->sectors - run.sector) return -EINVAL;
    if (run.type != 0 && run.type != 2) {  /* runs with stored bytes: inside the data fork */
      if (ds > t->dfl || run.off > t->dfl - ds || run.len > t->dfl - ds - run.off) return -EINVAL;
    }
    run.off += t->dfo + ds;
    if (t->n == t->cap) {
      size_t c = t->cap ? t->cap * 2 : 64;
      udif_run *g = realloc(t->runs, c * sizeof *g);
      if (!g) return -ENOMEM;
      t->runs = g; t->cap = c;
    }
    t->runs[t->n++] = run;
  }
  return 0;
}

static int by_sector(const void *a, const void *b) {
  const udif_run *x = a, *y = b;
  return x->sector < y->sector ? -1 : x->sector > y->sector;
}

int udif_table_open(const udif_reader *r, udif_table **out) {
  *out = NULL;
  if (r->size < 512) return -EINVAL;
  uint8_t k[512];
  int e = r->pread(r->ctx, k, 512, r->size - 512);
  if (e) return e < 0 ? e : -EIO;
  if (memcmp(k, "koly", 4)) return -EINVAL;
  uint64_t body = r->size - 512;
  uint64_t dfo = be64(k + 0x18), dfl = be64(k + 0x20), xo = be64(k + 0xd8), xl = be64(k + 0xe0);
  if (xl == 0 || xl > MAX_XML || xo > body || xl > body - xo) return -EINVAL;
  if (dfo > body || dfl > body - dfo) return -EINVAL;
  /* every byte offset into the image, sector * 512, must fit an off_t: runs lie within the sectors */
  if (be64(k + 0x1ec) > (uint64_t)INT64_MAX / 512) return -EINVAL;
  udif_table *t = calloc(1, sizeof *t);
  char *xml = malloc(xl);
  if (!t || !xml) { free(t); free(xml); return -ENOMEM; }
  t->sectors = be64(k + 0x1ec); t->dfo = dfo; t->dfl = dfl;
  t->fork_type = be32(k + 0x50); t->fork_crc = be32(k + 0x58);
  t->master_type = be32(k + 0x160); t->master_crc = be32(k + 0x168);
  e = r->pread(r->ctx, xml, xl, xo);
  if (!e) e = udif_plist_blkx(xml, xl, add_mish, t);
  free(xml);
  if (!e && t->n == 0) e = -EINVAL;
  if (!e) {
    qsort(t->runs, t->n, sizeof *t->runs, by_sector);
    for (size_t i = 1; i < t->n; i++)
      if (t->runs[i - 1].sector + t->runs[i - 1].count > t->runs[i].sector) { e = -EINVAL; break; }
  }
  if (e) { udif_table_close(t); return e < 0 ? e : -EIO; }
  *out = t; return 0;
}

uint64_t udif_table_sectors(const udif_table *t) { return t->sectors; }
size_t udif_table_runs(const udif_table *t, const udif_run **runs) { *runs = t->runs; return t->n; }
int udif_table_has_type(const udif_table *t, uint32_t type) {
  for (size_t i = 0; i < t->n; i++) if (t->runs[i].type == type) return 1;
  return 0;
}
void udif_table_checksums(const udif_table *t, udif_checksums *c) {
  c->nmish = t->nmish; c->mish_type = t->mish_type; c->mish_crc = t->mish_crc;
  c->master_type = t->master_type; c->master_crc = t->master_crc; c->fork_type = t->fork_type; c->fork_crc = t->fork_crc;
}
void udif_table_fork(const udif_table *t, uint64_t *off, uint64_t *len) { *off = t->dfo; *len = t->dfl; }
void udif_table_close(udif_table *t) { if (t) { free(t->runs); free(t->mish_type); free(t->mish_crc); free(t); } }
