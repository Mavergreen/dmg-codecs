/* platform: host-agnostic -- udif_image_crc32 against the checksums hdiutil stores: per-mish CRC32 over the
 *           decoded sectors of every run but ignore runs, the master CRC32 over those, or else the data fork CRC32 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "check.h"
#include "mkudif.h"
#include "udif.h"

static const char *FIX;

static uint8_t *load(const char *name, size_t *n) {
  char p[1024]; snprintf(p, sizeof p, "%s/%s", FIX, name);
  FILE *f = fopen(p, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); rewind(f);
  uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; } fclose(f); return b;
}

/* 0 if the stored and recalculated checksums agree, -EBADMSG if not, else udif_image_crc32's error */
static int verify_mem(const uint8_t *p, size_t n) {
  mk_mem m = { p, n }; udif_reader r = mk_reader(&m); udif_image *im;
  int e = udif_image_open(&r, &im);
  if (e) return 1000 + e;
  uint32_t stored = 0, calculated = 0, cheap = 1;
  int ce = udif_image_stored_crc32(im, &cheap);
  e = udif_image_crc32(im, &stored, &calculated);
  udif_image_close(im);
  /* the cheap read of the stored value agrees with the full one, error for error */
  CHECK_EQ(ce, e == -ENOENT ? -ENOENT : 0);
  if (!e) CHECK_EQ(cheap, stored);
  return e ? e : stored == calculated ? 0 : -EBADMSG;
}

static uint64_t be64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = v << 8 | p[i]; return v; }

static void fixtures(void) {
  const char *names[] = { "ulfo.dmg", "ulmo.dmg", "udzo.dmg", "udbz.dmg", "udro.dmg", "ulfo-huge.dmg" };
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    size_t n; uint8_t *d = load(names[i], &n); CHECK(d != NULL); if (!d) continue;
    int e = verify_mem(d, n);
    if (e) fprintf(stderr, "%s: verify %d\n", names[i], e);
    CHECK_EQ(e, 0);
    free(d);
  }
}

static void mismatches(void) {
  size_t n; uint8_t *d = load("udro.dmg", &n); CHECK(d != NULL); if (!d) return;
  uint8_t *b = malloc(n);
  /* a byte of a raw (type 1) run, from the uncompressed fixture: reads fine, its mish CRC no longer matches */
  mk_mem m = { d, n }; udif_reader r = mk_reader(&m); udif_table *t; const udif_run *runs = NULL; size_t nr = 0;
  CHECK_EQ(udif_table_open(&r, &t), 0);
  if (t) nr = udif_table_runs(t, &runs);
  uint64_t raw = 0; for (size_t i = 0; i < nr; i++) if (runs[i].type == 1) { raw = runs[i].off + 7; break; }
  if (t) udif_table_close(t);
  CHECK(raw != 0);
  memcpy(b, d, n); b[raw] ^= 0x01;
  CHECK_EQ(verify_mem(b, n), -EBADMSG);
  /* the master checksum alone wrong */
  memcpy(b, d, n); b[n - 512 + 0x168] ^= 0x01;
  CHECK_EQ(verify_mem(b, n), -EBADMSG);
  /* no master checksum, only the data fork's: right, then wrong */
  const uint8_t *k = d + n - 512; uint64_t dfo = be64(k + 0x18), dfl = be64(k + 0x20);
  uint32_t crc = (uint32_t)crc32(0, d + dfo, (uInt)dfl);
  memcpy(b, d, n); mk_be(b + n - 512 + 0x160, 0, 4); mk_be(b + n - 512 + 0x50, 2, 4); mk_be(b + n - 512 + 0x54, 32, 4); mk_be(b + n - 512 + 0x58, crc, 4);
  CHECK_EQ(verify_mem(b, n), 0);
  mk_be(b + n - 512 + 0x58, crc ^ 1, 4);
  CHECK_EQ(verify_mem(b, n), -EBADMSG);
  free(b); free(d);
}

static void types(void) {
  const char *names[] = { "ulfo.dmg", "ulmo.dmg" };
  for (int i = 0; i < 2; i++) {
    size_t n; uint8_t *d = load(names[i], &n); CHECK(d != NULL); if (!d) continue;
    mk_mem m = { d, n }; udif_reader r = mk_reader(&m); udif_image *im = NULL;
    CHECK_EQ(udif_image_open(&r, &im), 0);
    if (im) { CHECK_EQ(udif_image_has_type(im, 0x80000008), i == 1); CHECK_EQ(udif_image_has_type(im, 0x80000007), i == 0); udif_image_close(im); }
    free(d);
  }
}

static void none(void) {
  /* mk_udif stores no checksums at all */
  static uint8_t raw[1024]; memset(raw, 0x5a, sizeof raw);
  mk_run runs[] = { { 1, 2, raw, 1024 } };
  size_t n; uint8_t *img = mk_udif(runs, 1, NULL, &n);
  CHECK_EQ(verify_mem(img, n), -ENOENT);
  free(img);
}

int main(int argc, char **argv) {
  FIX = argc > 1 ? argv[1] : "tests/fixtures";
  fixtures(); mismatches(); types(); none();
  return CHECK_DONE();
}
