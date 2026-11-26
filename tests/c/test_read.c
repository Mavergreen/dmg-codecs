/* platform: host-agnostic -- decoded sectors: every fixture byte-exact, odd reads, two images at once, bad chunks, ADC */
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "fileread.h"
#include "mkudif.h"
#include "sha256.h"
#include "udif.h"
#include <zlib.h>
#include <bzlib.h>

static const char *FIX;

static int expected(const char *name, uint64_t *sectors, char hex[65]) {
  char p[1024], n[64]; FILE *f; snprintf(p, sizeof p, "%s/expected.txt", FIX);
  if (!(f = fopen(p, "r"))) return -1;
  unsigned long long s;
  while (fscanf(f, "%63s %llu %64s", n, &s, hex) == 3) if (!strncmp(n, name, strlen(n)) && !strcmp(name + strlen(n), ".dmg")) { fclose(f); *sectors = s; return 0; }
  fclose(f); return -1;
}

static uint8_t *load(const char *name, size_t *n) {
  char p[1024]; snprintf(p, sizeof p, "%s/%s", FIX, name);
  FILE *f = fopen(p, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); rewind(f);
  uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; } fclose(f); return b;
}

/* the whole image, read in 64 KiB pieces, hashes to expected.txt; returns the image when small enough to keep */
static uint8_t *whole(const char *name) {
  uint64_t sectors; char want[65], got[65];
  if (expected(name, &sectors, want)) { CHECK(0); return NULL; }
  int fd; udif_reader r; udif_image *im = NULL;
  if (fr_open(FIX, name, &fd, &r)) { CHECK(0); return NULL; }
  CHECK_EQ(udif_image_open(&r, &im), 0);
  if (!im) { close(fd); return NULL; }
  CHECK_EQ(udif_image_size(im), sectors * 512);
  uint8_t *keep = sectors * 512 <= (64 << 20) ? malloc(sectors * 512) : NULL;
  static uint8_t buf[65536]; sha256 s; sha_init(&s);
  for (uint64_t off = 0; off < sectors * 512; off += sizeof buf) {
    size_t n = sectors * 512 - off < sizeof buf ? (size_t)(sectors * 512 - off) : sizeof buf;
    int e = udif_image_read(im, buf, n, off);
    if (e) { CHECK_EQ(e, 0); break; }
    sha_update(&s, buf, n); if (keep) memcpy(keep + off, buf, n);
  }
  sha_hex(&s, got);
  if (strcmp(got, want)) { fprintf(stderr, "%s: sha256 %s, want %s\n", name, got, want); CHECK(0); }
  udif_image_close(im); close(fd);
  return keep;
}

static void odd_reads(const char *name, const uint8_t *img) {
  int fd; udif_reader r; udif_image *im = NULL;
  if (!img || fr_open(FIX, name, &fd, &r)) { CHECK(0); return; }
  CHECK_EQ(udif_image_open(&r, &im), 0);
  if (!im) { close(fd); return; }
  uint64_t size = udif_image_size(im);
  const struct { uint64_t off; size_t len; } c[] = {
    { 0, 1 }, { 1, 511 }, { 511, 4097 }, { 4096 * 3 + 7, 200000 }, { size - 1, 1 }, { size - 4097, 4097 }, { 777, 1 }
  };
  uint8_t *buf = malloc(200000);
  for (size_t i = 0; i < sizeof c / sizeof *c; i++) {
    if (c[i].off + c[i].len > size) continue;
    CHECK_EQ(udif_image_read(im, buf, c[i].len, c[i].off), 0);
    if (memcmp(buf, img + c[i].off, c[i].len)) { fprintf(stderr, "%s: read(%llu, %zu) differs\n", name, (unsigned long long)c[i].off, c[i].len); CHECK(0); }
  }
  CHECK_EQ(udif_image_read(im, buf, 2, size - 1), -EINVAL);  /* past the end */
  free(buf); udif_image_close(im); close(fd);
}

static void huge(void) {
  /* reads around 4 GiB, at two alignments, agree */
  int fd; udif_reader r; udif_image *im = NULL;
  if (fr_open(FIX, "ulfo-huge.dmg", &fd, &r)) { CHECK(0); return; }
  CHECK_EQ(udif_image_open(&r, &im), 0);
  if (!im) { close(fd); return; }
  uint64_t g4 = (uint64_t)4 << 30;
  static uint8_t a[1 << 20], b[1 << 20];
  CHECK_EQ(udif_image_read(im, a, sizeof a, g4 - 512 * 1024), 0);
  CHECK_EQ(udif_image_read(im, b, sizeof b - 3, g4 - 512 * 1024 + 3), 0);
  CHECK(!memcmp(a + 3, b, sizeof b - 3));
  CHECK_EQ(udif_image_read(im, a, 4096, udif_image_size(im) - 4096), 0);
  udif_image_close(im); close(fd);
}

struct th { const char *name; const uint8_t *img; int bad; };
static void *reader_thread(void *v) {
  struct th *t = v; int fd; udif_reader r; udif_image *im = NULL;
  if (fr_open(FIX, t->name, &fd, &r) || udif_image_open(&r, &im)) { t->bad = 1; return NULL; }
  uint64_t size = udif_image_size(im); uint8_t buf[8192];
  for (int pass = 0; pass < 20; pass++)
    for (uint64_t off = (uint64_t)pass * 512; off + sizeof buf <= size; off += 37 * 4096)
      if (udif_image_read(im, buf, sizeof buf, off) || memcmp(buf, t->img + off, sizeof buf)) t->bad = 1;
  udif_image_close(im); close(fd); return NULL;
}
static void two_at_once(const uint8_t *ulfo, const uint8_t *ulmo) {
  if (!ulfo || !ulmo) { CHECK(0); return; }
  struct th a = { "ulfo.dmg", ulfo, 0 }, b = { "ulfo.dmg", ulfo, 0 }, c = { "ulmo.dmg", ulmo, 0 };
  pthread_t ta, tb, tc;
  pthread_create(&ta, NULL, reader_thread, &a); pthread_create(&tb, NULL, reader_thread, &b); pthread_create(&tc, NULL, reader_thread, &c);
  pthread_join(ta, NULL); pthread_join(tb, NULL); pthread_join(tc, NULL);
  CHECK(!a.bad); CHECK(!b.bad); CHECK(!c.bad);
}

/* one byte flipped in the first run of TYPE: reads touching it fail, the rest still match */
static void bad_chunk(const char *name, uint32_t type, const uint8_t *img) {
  size_t n; uint8_t *b = load(name, &n);
  if (!b || !img) { CHECK(0); free(b); return; }
  mk_mem m = { b, n }; udif_reader r = mk_reader(&m); udif_table *t = NULL;
  CHECK_EQ(udif_table_open(&r, &t), 0);
  if (!t) { free(b); return; }
  const udif_run *runs; size_t k = udif_table_runs(t, &runs), i;
  for (i = 0; i < k && runs[i].type != type; i++) ;
  if (i == k) { CHECK(0); udif_table_close(t); free(b); return; }
  udif_run hit = runs[i], other = runs[i ? 0 : k - 1];
  udif_table_close(t);
  for (uint64_t j = 0; j < hit.len; j += 97) b[hit.off + j] ^= 0x55;  /* spec: one flip can survive a codec; many cannot */
  udif_image *im = NULL;
  CHECK_EQ(udif_image_open(&r, &im), 0);
  if (im) {
    uint8_t buf[512];
    CHECK_EQ(udif_image_read(im, buf, 512, hit.sector * 512), -EIO);
    CHECK_EQ(udif_image_read(im, buf, 512, other.sector * 512), 0);
    CHECK(!memcmp(buf, img + other.sector * 512, 512));
    udif_image_close(im);
  }
  free(b);
}

static void adc(void) {
  /* spec: no tool writes ADC now, so two hand-built runs: 3-byte copies ("ABCD" x128), 2-byte copies ("xy" x256) */
  uint8_t a[64], x[64]; size_t na = 0, nx = 0;
  a[na++] = 0x83; memcpy(a + na, "ABCD", 4); na += 4;
  for (int i = 0; i < 7; i++) { a[na++] = 0x40 | (67 - 4); a[na++] = 0; a[na++] = 3; }
  a[na++] = 0x40 | (39 - 4); a[na++] = 0; a[na++] = 3;
  x[nx++] = 0x81; x[nx++] = 'x'; x[nx++] = 'y';
  for (int i = 0; i < 28; i++) { x[nx++] = (18 - 3) << 2; x[nx++] = 1; }
  x[nx++] = (6 - 3) << 2; x[nx++] = 1;
  mk_run runs[] = { { 0x80000004, 1, a, na }, { 0x80000004, 1, x, nx }, { 0x80000009, 1, a, na } };
  size_t n; uint8_t *img = mk_udif(runs, 3, NULL, &n);
  mk_mem m = { img, n }; udif_reader r = mk_reader(&m); udif_image *im = NULL;
  CHECK_EQ(udif_image_open(&r, &im), 0);
  if (im) {
    uint8_t buf[1024], want[1024];
    for (int i = 0; i < 512; i++) { want[i] = "ABCD"[i % 4]; want[512 + i] = "xy"[i % 2]; }
    CHECK_EQ(udif_image_read(im, buf, 1024, 0), 0);
    CHECK(!memcmp(buf, want, 1024));
    CHECK_EQ(udif_image_read(im, buf, 512, 1024), -EIO);  /* an unknown chunk type: open accepts it, reads of it fail */
    udif_image_close(im);
  }
  free(img);
}

/* chunks whose stream decodes to more or fewer bytes than their sectors are refused */
static void lying_lengths(void) {
  static uint8_t plain[1024], z[2048], bz[2048];
  for (int i = 0; i < 1024; i++) plain[i] = (uint8_t)(i * 7);
  uLongf zl = sizeof z; unsigned bl = sizeof bz;
  CHECK_EQ(compress(z, &zl, plain, 1024), Z_OK);
  CHECK_EQ(BZ2_bzBuffToBuffCompress((char *)bz, &bl, (char *)plain, 1024, 9, 0, 0), BZ_OK);
  for (int k = 0; k < 2; k++) {
    uint32_t t = k ? 0x80000006 : 0x80000005; const uint8_t *d = k ? bz : z; size_t n = k ? bl : zl;
    for (uint64_t sectors = 1; sectors <= 3; sectors += 2) {  /* 1024 bytes decoded into 1 sector, then into 3 */
      mk_run runs[] = { { t, sectors, d, n } };
      size_t len; uint8_t *img = mk_udif(runs, 1, NULL, &len);
      mk_mem m = { img, len }; udif_reader r = mk_reader(&m); udif_image *im = NULL;
      CHECK_EQ(udif_image_open(&r, &im), 0);
      if (im) { uint8_t buf[512]; CHECK_EQ(udif_image_read(im, buf, 512, 0), -EIO); udif_image_close(im); }
      free(img);
    }
  }
}

/* sectors no run covers read as zeros: a comment run (skipped) leaves a gap before the first real run */
static void gaps(void) {
  static uint8_t raw[512]; memset(raw, 0xab, sizeof raw);
  static const uint8_t short_adc[] = { 0x83, 'A', 'B', 'C', 'D' };  /* 4 bytes where a sector needs 512 */
  mk_run runs[] = { { 0x7ffffffe, 2, NULL, 0 }, { 1, 1, raw, 512 }, { 0x80000004, 1, short_adc, sizeof short_adc } };
  size_t n; uint8_t *img = mk_udif(runs, 3, NULL, &n);
  mk_mem m = { img, n }; udif_reader r = mk_reader(&m); udif_image *im = NULL;
  CHECK_EQ(udif_image_open(&r, &im), 0);
  if (im) {
    uint8_t buf[1536], want[1536] = {0};
    memset(want + 1024, 0xab, 512);
    CHECK_EQ(udif_image_read(im, buf, 1536, 0), 0);
    CHECK(!memcmp(buf, want, 1536));
    CHECK_EQ(udif_image_read(im, buf, 512, 1536), -EIO);  /* an ADC stream that ends early */
    udif_image_close(im);
  }
  free(img);
}

int main(int argc, char **argv) {
  FIX = argc > 1 ? argv[1] : "tests/fixtures";
  uint8_t *ulfo = whole("ulfo.dmg"), *ulmo = whole("ulmo.dmg"), *udzo = whole("udzo.dmg");
  free(whole("udbz.dmg")); free(whole("udro.dmg")); free(whole("ulfo-huge.dmg"));
  odd_reads("ulfo.dmg", ulfo); odd_reads("ulmo.dmg", ulmo); odd_reads("udzo.dmg", udzo);
  huge();
  two_at_once(ulfo, ulmo);
  bad_chunk("ulfo.dmg", 0x80000007, ulfo); bad_chunk("ulmo.dmg", 0x80000008, ulmo); bad_chunk("udzo.dmg", 0x80000005, udzo);
  adc();
  lying_lengths();
  gaps();
  free(ulfo); free(ulmo); free(udzo);
  return CHECK_DONE();
}
