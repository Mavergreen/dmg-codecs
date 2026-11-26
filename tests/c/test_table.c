/* platform: host-agnostic -- the trailer and run table: real fixtures, then malformed images */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "fileread.h"
#include "mkudif.h"
#include "udif.h"

static const char *FIX;

static void fixture(const char *name, uint64_t sectors, int lzfse, int xz) {
  int fd; udif_reader r; udif_table *t = NULL;
  if (fr_open(FIX, name, &fd, &r)) { CHECK(0); return; }
  CHECK_EQ(udif_table_open(&r, &t), 0);
  if (t) {
    CHECK_EQ(udif_table_sectors(t), sectors);
    CHECK_EQ(udif_table_has_type(t, 0x80000007), lzfse);
    CHECK_EQ(udif_table_has_type(t, 0x80000008), xz);
    const udif_run *runs; size_t n = udif_table_runs(t, &runs);
    CHECK(n > 0);
    for (size_t i = 1; i < n; i++) CHECK(runs[i - 1].sector + runs[i - 1].count <= runs[i].sector);
    for (size_t i = 0; i < n; i++) CHECK(runs[i].type != 0xffffffff && runs[i].type != 0x7ffffffe);
    udif_table_close(t);
  }
  close(fd);
}

static int open_mem(const uint8_t *p, size_t n) {
  mk_mem m = { p, n }; udif_reader r = mk_reader(&m); udif_table *t = NULL;
  int e = udif_table_open(&r, &t);
  if (t) udif_table_close(t);
  return e;
}

static void malformed(void) {
  static uint8_t raw[1024];
  memset(raw, 0x5a, sizeof raw);
  mk_run runs[] = { { 1, 2, raw, 1024 }, { 0, 6, NULL, 0 } };
  size_t n; uint8_t *img = mk_udif(runs, 2, NULL, &n);
  CHECK_EQ(open_mem(img, n), 0);

  uint8_t zeros[512] = {0};
  CHECK_EQ(open_mem(zeros, sizeof zeros), -EINVAL);   /* no koly */
  CHECK_EQ(open_mem(zeros, 100), -EINVAL);            /* shorter than a trailer */
  CHECK_EQ(open_mem(img, n - 1), -EINVAL);            /* truncated by one byte */

  uint8_t *b = malloc(n);
  memcpy(b, img, n); mk_be(b + n - 512 + 0xd8, n + 4096, 8);        /* XML offset past EOF */
  CHECK_EQ(open_mem(b, n), -EINVAL);
  memcpy(b, img, n); mk_be(b + n - 512 + 0xe0, (uint64_t)1 << 62, 8); /* XML length absurd */
  CHECK_EQ(open_mem(b, n), -EINVAL);

  memcpy(b, img, n);                                                  /* Data not base64 */
  char *d = memmem(b, n, "<data>", 6); CHECK(d != NULL);
  if (d) { char *q = d + 6; while (*q == '\n' || *q == '\t') q++; q[0] = '!'; q[1] = '!'; }
  CHECK_EQ(open_mem(b, n), -EINVAL);
  free(b); free(img);

  /* a run whose data passes the end of the data fork */
  mk_run big[] = { { 0x80000005, 2, raw, 100 } };
  img = mk_udif(big, 1, NULL, &n);
  /* patch the koly data fork length down so the run (100 bytes at 0) overruns it */
  b = malloc(n); memcpy(b, img, n); mk_be(b + n - 512 + 0x20, 50, 8);
  CHECK_EQ(open_mem(b, n), -EINVAL);
  free(b); free(img);

  /* sectors beyond what a byte offset can hold: count * 512 would wrap, and the image would read past its cache slot */
  mk_run vast[] = { { 0x80000005, ((uint64_t)1 << 55) + 1, raw, 100 }, { 0, 7, NULL, 0 } };
  img = mk_udif(vast, 2, NULL, &n);
  CHECK_EQ(open_mem(img, n), -EINVAL);
  free(img);

  /* a mish that claims more runs than it holds */
  /* one valid zero run, then a count of 1000 where only that one fits */
  uint8_t mish[0xcc + 40] = {0};
  memcpy(mish, "mish", 4); mk_be(mish + 0x10, 1, 8); mk_be(mish + 0xc8, 1000, 4); mk_be(mish + 0xcc + 16, 1, 8);
  mk_buf x = {0};
  mk_str(&x, "<plist><dict><key>resource-fork</key><dict><key>blkx</key><array><dict><key>Data</key><data>");
  mk_b64(&x, mish, sizeof mish);
  mk_str(&x, "</data></dict></array></dict></dict></plist>");
  uint8_t k[512] = {0}; memcpy(k, "koly", 4); mk_be(k + 0xd8, 0, 8); mk_be(k + 0xe0, x.len, 8); mk_be(k + 0x1ec, 1, 8);
  mk_put(&x, k, 512);
  CHECK_EQ(open_mem(x.buf, x.len), -EINVAL);
  free(x.buf);
}

static void extras(void) {
  /* spec: modern hdiutil writes cSum, plst and nsiz resources beside blkx, and entities in Name */
  static uint8_t raw[512];
  mk_run runs[] = { { 1, 1, raw, 512 } };
  const char *extra =
    "\t\t<key>cSum</key>\n\t\t<array>\n\t\t\t<dict>\n\t\t\t\t<key>Data</key>\n\t\t\t\t<data>AQACAAAAAAA=</data>\n\t\t\t\t<key>Name</key>\n\t\t\t\t<string>&lt;cSum&gt;</string>\n\t\t\t</dict>\n\t\t</array>\n"
    "\t\t<key>nsiz</key>\n\t\t<array>\n\t\t\t<dict>\n\t\t\t\t<key>Data</key>\n\t\t\t\t<data>PD94bWw=</data>\n\t\t\t</dict>\n\t\t</array>\n"
    "\t\t<key>plst</key>\n\t\t<array>\n\t\t\t<dict>\n\t\t\t\t<key>Data</key>\n\t\t\t\t<data>AAAA</data>\n\t\t\t</dict>\n\t\t</array>\n";
  size_t n; uint8_t *img = mk_udif(runs, 1, extra, &n);
  mk_mem m = { img, n }; udif_reader r = mk_reader(&m); udif_table *t = NULL;
  CHECK_EQ(udif_table_open(&r, &t), 0);
  if (t) { const udif_run *rs; CHECK_EQ(udif_table_runs(t, &rs), 1); CHECK_EQ(udif_table_sectors(t), 1); udif_table_close(t); }
  free(img);
}

int main(int argc, char **argv) {
  FIX = argc > 1 ? argv[1] : "tests/fixtures";
  fixture("ulfo.dmg", 4161, 1, 0);
  fixture("ulmo.dmg", 4161, 0, 1);
  fixture("udzo.dmg", 4161, 0, 0);
  fixture("udbz.dmg", 4161, 0, 0);
  fixture("udro.dmg", 4161, 0, 0);
  fixture("ulfo-huge.dmg", 10485760, 1, 0);
  malformed();
  extras();
  return CHECK_DONE();
}
