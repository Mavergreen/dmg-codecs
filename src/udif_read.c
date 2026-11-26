#include <bzlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "lzfse.h"
#include "xz.h"
#include "udif.h"

/* spec: a compressed chunk decoding to more than this is refused rather than allocated. Apple's own reader allows
 *       0x200000 sectors; modern hdiutil writes 2048-sector chunks unless told otherwise. */
#define MAX_CHUNK ((uint64_t)64 << 20)
#define CACHE 4

struct slot { size_t run; uint8_t *buf; size_t cap; unsigned long used; };
struct udif_image {
  udif_reader r;
  udif_table *t;
  const udif_run *runs; size_t n;
  uint64_t size;
  pthread_mutex_t lock;
  struct slot cache[CACHE];
  unsigned long tick;
  uint8_t *in; size_t incap;
};

static pthread_once_t xz_once = PTHREAD_ONCE_INIT;
static void xz_tables(void) { xz_crc32_init(); xz_crc64_init(); }

int udif_image_open(const udif_reader *r, udif_image **out) {
  *out = NULL;
  udif_image *im = calloc(1, sizeof *im);
  if (!im) return -ENOMEM;
  im->r = *r;
  int e = udif_table_open(&im->r, &im->t);
  if (e) { free(im); return e; }
  im->n = udif_table_runs(im->t, &im->runs);
  im->size = udif_table_sectors(im->t) * 512;
  for (int i = 0; i < CACHE; i++) im->cache[i].run = (size_t)-1;
  pthread_mutex_init(&im->lock, NULL);
  pthread_once(&xz_once, xz_tables);
  *out = im; return 0;
}

uint64_t udif_image_size(const udif_image *im) { return im->size; }

static int is_coded(uint32_t t) { return t >= 0x80000004 && t <= 0x80000008; }

/* decodes run i into a cache slot (lock held); returns the slot or NULL */
static struct slot *decoded(udif_image *im, size_t i, int *err) {
  struct slot *s = NULL;
  for (int k = 0; k < CACHE; k++) if (im->cache[k].run == i) { s = &im->cache[k]; s->used = ++im->tick; return s; }
  const udif_run *r = &im->runs[i];
  uint64_t outlen = r->count * 512;
  if (outlen > MAX_CHUNK || r->len > MAX_CHUNK) { *err = -EIO; return NULL; }
  s = &im->cache[0];
  for (int k = 1; k < CACHE; k++) if (im->cache[k].used < s->used) s = &im->cache[k];
  s->run = (size_t)-1;
  /* one spare byte: a chunk that decodes to more than its sectors fills it, and is refused */
  if (s->cap < outlen + 1) { uint8_t *g = realloc(s->buf, outlen + 1); if (!g) { *err = -ENOMEM; return NULL; } s->buf = g; s->cap = outlen + 1; }
  if (im->incap < r->len) { uint8_t *g = realloc(im->in, r->len); if (!g) { *err = -ENOMEM; return NULL; } im->in = g; im->incap = r->len; }
  if (im->r.pread(im->r.ctx, im->in, r->len, r->off)) { *err = -EIO; return NULL; }
  int ok = 0;
  switch (r->type) {
  case 0x80000004: ok = !udif_adc_decode(im->in, r->len, s->buf, outlen); break;
  case 0x80000005: { uLongf o = outlen + 1; ok = uncompress(s->buf, &o, im->in, r->len) == Z_OK && o == outlen; break; }
  case 0x80000006: { unsigned o = (unsigned)outlen + 1; ok = BZ2_bzBuffToBuffDecompress((char *)s->buf, &o, (char *)im->in, (unsigned)r->len, 0, 0) == BZ_OK && o == outlen; break; }
  case 0x80000007: ok = lzfse_decode_buffer(s->buf, outlen + 1, im->in, r->len, NULL) == outlen; break;
  case 0x80000008: {
    struct xz_dec *d = xz_dec_init(XZ_SINGLE, 0);
    if (!d) { *err = -ENOMEM; return NULL; }
    struct xz_buf b = { im->in, 0, r->len, s->buf, 0, outlen + 1 };
    ok = xz_dec_run(d, &b) == XZ_STREAM_END && b.out_pos == outlen;
    xz_dec_end(d); break;
  }
  }
  if (!ok) { *err = -EIO; return NULL; }
  s->run = i; s->used = ++im->tick;
  return s;
}

int udif_image_read(udif_image *im, void *buf, size_t len, uint64_t off) {
  if (off > im->size || len > im->size - off) return -EINVAL;
  uint8_t *dst = buf; size_t done = 0; int e = 0;
  pthread_mutex_lock(&im->lock);
  while (done < len && !e) {
    uint64_t pos = off + done, sec = pos / 512;
    size_t lo = 0, hi = im->n;  /* the last run starting at or before sec */
    while (hi - lo > 1) { size_t mid = lo + (hi - lo) / 2; if (im->runs[mid].sector <= sec) lo = mid; else hi = mid; }
    const udif_run *r = &im->runs[lo];
    size_t n;
    if (sec < r->sector || sec >= r->sector + r->count) {  /* no run here: zeros up to the next one */
      uint64_t next = sec < r->sector ? r->sector * 512 : (hi < im->n ? im->runs[hi].sector * 512 : im->size);
      n = next - pos < len - done ? (size_t)(next - pos) : len - done;
      memset(dst + done, 0, n);
    } else {
      uint64_t in = pos - r->sector * 512, avail = r->count * 512 - in;
      n = avail < len - done ? (size_t)avail : len - done;
      if (r->type == 0 || r->type == 2) memset(dst + done, 0, n);
      else if (r->type == 1) { if (r->len < r->count * 512 || im->r.pread(im->r.ctx, dst + done, n, r->off + in)) e = -EIO; }
      else if (is_coded(r->type)) { struct slot *s = decoded(im, lo, &e); if (s) memcpy(dst + done, s->buf + in, n); }
      else e = -EIO;
    }
    done += n;
  }
  pthread_mutex_unlock(&im->lock);
  return e;
}

void udif_image_close(udif_image *im) {
  if (!im) return;
  for (int i = 0; i < CACHE; i++) free(im->cache[i].buf);
  free(im->in);
  pthread_mutex_destroy(&im->lock);
  udif_table_close(im->t);
  free(im);
}
