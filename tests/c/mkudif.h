/* platform: host-agnostic -- builds UDIF images in memory for tests: a data fork of the given runs' bytes,
 * an XML plist (with optional extra text inside resource-fork), and a koly trailer. Malformed images are made by
 * editing the result. */
#ifndef DC_MKUDIF_H
#define DC_MKUDIF_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "udif.h"

typedef struct { uint32_t type; uint64_t count; const void *data; size_t len; } mk_run;
typedef struct { uint8_t *buf; size_t len, cap; } mk_buf;

static void mk_put(mk_buf *b, const void *p, size_t n) {
  if (b->len + n > b->cap) { b->cap = (b->len + n) * 2 + 64; b->buf = realloc(b->buf, b->cap); }
  memcpy(b->buf + b->len, p, n); b->len += n;
}
static void mk_str(mk_buf *b, const char *s) { mk_put(b, s, strlen(s)); }
static void mk_be(uint8_t *p, uint64_t v, int n) { for (int i = n - 1; i >= 0; i--) { p[i] = v & 0xff; v >>= 8; } }
static void mk_b64(mk_buf *b, const uint8_t *p, size_t n) {
  static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    char o[4] = { a[v >> 18 & 63], a[v >> 12 & 63], i + 1 < n ? a[v >> 6 & 63] : '=', i + 2 < n ? a[v & 63] : '=' };
    mk_put(b, o, 4);
    if (i % 54 == 51) mk_str(b, "\n\t\t\t");  /* spec: hdiutil wraps <data> lines; the parser must accept that */
  }
}
/* one blkx entry holding all runs, starting at sector 0; extra is inserted inside resource-fork before blkx */
static uint8_t *mk_udif(const mk_run *runs, int n, const char *extra, size_t *outlen) {
  mk_buf img = {0}, mish = {0}, xml = {0};
  uint64_t sectors = 0;
  uint8_t h[0xcc] = {0};
  memcpy(h, "mish", 4); mk_be(h + 4, 1, 4);
  for (int i = 0; i < n; i++) sectors += runs[i].count;
  mk_be(h + 0x10, sectors, 8); mk_be(h + 0xc8, n + 1, 4);
  mk_put(&mish, h, sizeof h);
  uint64_t s = 0;
  for (int i = 0; i < n; i++) {
    uint8_t r[40] = {0};
    mk_be(r, runs[i].type, 4); mk_be(r + 8, s, 8); mk_be(r + 16, runs[i].count, 8);
    mk_be(r + 24, img.len, 8); mk_be(r + 32, runs[i].len, 8);
    mk_put(&mish, r, 40); if (runs[i].len) mk_put(&img, runs[i].data, runs[i].len);
    s += runs[i].count;
  }
  uint8_t end[40] = {0}; mk_be(end, 0xffffffff, 4); mk_be(end + 8, s, 8); mk_put(&mish, end, 40);
  uint64_t dfl = img.len;
  mk_str(&xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n\t<key>resource-fork</key>\n\t<dict>\n");
  if (extra) mk_str(&xml, extra);
  mk_str(&xml, "\t\t<key>blkx</key>\n\t\t<array>\n\t\t\t<dict>\n\t\t\t\t<key>Attributes</key>\n\t\t\t\t<string>0x0050</string>\n\t\t\t\t<key>Data</key>\n\t\t\t\t<data>\n\t\t\t");
  mk_b64(&xml, mish.buf, mish.len);
  mk_str(&xml, "\n\t\t\t\t</data>\n\t\t\t\t<key>Name</key>\n\t\t\t\t<string>disk image (Apple_HFS : 1) &amp; more</string>\n\t\t\t</dict>\n\t\t</array>\n\t</dict>\n</dict>\n</plist>\n");
  uint64_t xo = img.len;
  mk_put(&img, xml.buf, xml.len);
  uint8_t k[512] = {0};
  memcpy(k, "koly", 4); mk_be(k + 4, 4, 4); mk_be(k + 8, 512, 4);
  mk_be(k + 0x20, dfl, 8); mk_be(k + 0xd8, xo, 8); mk_be(k + 0xe0, xml.len, 8); mk_be(k + 0x1ec, sectors, 8);
  mk_put(&img, k, 512);
  free(mish.buf); free(xml.buf);
  *outlen = img.len; return img.buf;
}

typedef struct { const uint8_t *p; size_t n; } mk_mem;
static int mk_mem_pread(void *ctx, void *buf, size_t len, uint64_t off) {
  mk_mem *m = ctx; if (off > m->n || len > m->n - off) return -5; memcpy(buf, m->p + off, len); return 0;
}
static udif_reader mk_reader(mk_mem *m) { udif_reader r = { mk_mem_pread, m->n, m }; return r; }
#endif
