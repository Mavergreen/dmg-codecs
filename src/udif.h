/* libudif: reads Apple UDIF disk images (.dmg) -- the koly trailer, the blkx run table, and (udif_image_*) the
 * decompressed sectors. Portable C11; no CoreFoundation, so it builds and is tested on Linux too. */
#ifndef DC_UDIF_H
#define DC_UDIF_H
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t type; uint64_t sector, count, off, len; } udif_run;

/* How bytes are read: a file in tests and tools, the framework's backing store in the plug-in. */
typedef struct udif_reader {
  int (*pread)(void *ctx, void *buf, size_t len, uint64_t off);  /* 0 or -errno */
  uint64_t size;
  void *ctx;
} udif_reader;

typedef struct udif_table udif_table;
int udif_table_open(const udif_reader *r, udif_table **out);  /* 0, -EINVAL (not UDIF, malformed), -ENOMEM, -EIO */
uint64_t udif_table_sectors(const udif_table *t);
size_t udif_table_runs(const udif_table *t, const udif_run **runs);
int udif_table_has_type(const udif_table *t, uint32_t type);
void udif_table_close(udif_table *t);

/* internal to libudif: calls each(ctx, data, n) for every blkx resource's decoded Data; 0 or -EINVAL/-ENOMEM */
int udif_plist_blkx(const char *xml, size_t len, int (*each)(void *ctx, const uint8_t *data, size_t n), void *ctx);
#endif
