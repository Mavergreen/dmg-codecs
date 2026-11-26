/* libudif: reads Apple UDIF disk images (.dmg) -- the koly trailer, the blkx run table, and (udif_image_*) the
 * decompressed sectors. Portable C11; no CoreFoundation, so it builds and is tested on Linux too. */
#ifndef DC_UDIF_H
#define DC_UDIF_H
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t type; uint64_t sector, count, off, len; uint32_t mish; } udif_run;  /* mish: its blkx entry, in plist order */

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
/* spec: the CRC32 checksums a UDIF image stores (type 2; anything else is not stored): each blkx entry's over the
 *       decoded sectors of its runs but ignore runs, in run order; the master over those, as big-endian words;
 *       the data fork's over its bytes */
typedef struct { size_t nmish; const uint32_t *mish_type, *mish_crc; uint32_t master_type, master_crc, fork_type, fork_crc; } udif_checksums;
void udif_table_checksums(const udif_table *t, udif_checksums *c);
void udif_table_fork(const udif_table *t, uint64_t *off, uint64_t *len);  /* the data fork, in the file */

typedef struct udif_image udif_image;  /* a table plus its own chunk cache; one per attach, safe across threads */
int udif_image_open(const udif_reader *r, udif_image **out);  /* as udif_table_open */
uint64_t udif_image_size(const udif_image *im);  /* bytes */
int udif_image_has_type(const udif_image *im, uint32_t type);  /* as udif_table_has_type */
/* 0; -EINVAL if [off, off+len) passes the end; -EIO if a chunk it touches cannot be read or decoded */
int udif_image_read(udif_image *im, void *buf, size_t len, uint64_t off);
/* the image's own checksum -- the master CRC32 if stored, else the data fork's -- and the same recalculated from
 * what libudif serves. 0; -ENOENT if neither is stored; -EIO if a chunk cannot be read or decoded */
int udif_image_crc32(udif_image *im, uint32_t *stored, uint32_t *calculated);
/* the stored value alone, without reading the image: 0 or -ENOENT */
int udif_image_stored_crc32(const udif_image *im, uint32_t *stored);
void udif_image_close(udif_image *im);

/* internal to libudif: ADC (Apple Data Compression) into exactly outlen bytes; 0 or -EIO */
int udif_adc_decode(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);
/* internal to libudif: calls each(ctx, data, n) for every blkx resource's decoded Data; 0 or -EINVAL/-ENOMEM */
int udif_plist_blkx(const char *xml, size_t len, int (*each)(void *ctx, const uint8_t *data, size_t n), void *ctx);
#endif
