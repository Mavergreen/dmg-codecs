/* spec: a scanner for the one path libudif needs (resource-fork -> blkx -> each Data), not a plist parser:
 *       CoreFoundation is not on Linux, and the rest of the plist (cSum, plst, nsiz, Name) is never read. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "udif.h"

static const char *find(const char *p, const char *end, const char *s) {
  size_t n = strlen(s);
  for (; p && end - p >= (ptrdiff_t)n; p++) if (!memcmp(p, s, n)) return p;
  return NULL;
}
static const char *skip_ws(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}
static int b64(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
/* decodes [p, end) ignoring whitespace; *out is malloc'd */
static int decode(const char *p, const char *end, uint8_t **out, size_t *n) {
  uint8_t *o = malloc((size_t)(end - p) / 4 * 3 + 3);
  if (!o) return -ENOMEM;
  uint32_t acc = 0; int bits = 0, pad = 0; size_t k = 0;
  for (; p < end; p++) {
    char c = *p;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    if (c == '=') { pad++; continue; }
    int v = b64(c);
    if (v < 0 || pad) { free(o); return -EINVAL; }
    acc = acc << 6 | (uint32_t)v; bits += 6;
    if (bits >= 8) { bits -= 8; o[k++] = (uint8_t)(acc >> bits); }
  }
  *out = o; *n = k; return 0;
}

int udif_plist_blkx(const char *xml, size_t len, int (*each)(void *, const uint8_t *, size_t), void *ctx) {
  const char *end = xml + len;
  const char *rf = find(xml, end, "<key>resource-fork</key>");
  const char *bk = find(rf, end, "<key>blkx</key>");
  if (!bk) return -EINVAL;
  const char *a = skip_ws(bk + strlen("<key>blkx</key>"), end);
  if (end - a < 7 || memcmp(a, "<array>", 7)) return -EINVAL;
  const char *ae = find(a, end, "</array>");
  if (!ae) return -EINVAL;
  int found = 0;
  for (const char *p = a; (p = find(p, ae, "<key>Data</key>")) != NULL; ) {
    const char *d = skip_ws(p + strlen("<key>Data</key>"), ae);
    if (ae - d < 6 || memcmp(d, "<data>", 6)) return -EINVAL;
    const char *de = find(d, ae, "</data>");
    if (!de) return -EINVAL;
    uint8_t *bytes; size_t n;
    int e = decode(d + 6, de, &bytes, &n);
    if (e) return e;
    e = each(ctx, bytes, n);
    free(bytes);
    if (e) return e;
    found++; p = de;
  }
  return found ? 0 : -EINVAL;
}
