/* DMGCodecs.plugin: a DiskImages format plug-in that claims only UDIF images with LZFSE or xz chunks and serves their
 * sectors through libudif. Every other image is declined, so 10.9 treats it exactly as before. */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include "di_interface.h"
#include "udif.h"

#ifdef DC_TRACE
#define TRACE(...) syslog(LOG_NOTICE, "DMGCodecs: " __VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

/* platform: the framework's probe scores; stock UDIF returns 1000 for any UDIF store, and the highest wins */
#define SCORE_CLAIM 1001
#define SCORE_DECLINE (-1000)

typedef struct {
  const DIPlugInInterface *vtbl;
  int32_t refs;
  CFUUIDRef factory;
  udif_image *img;
  CBackingStore *open_store;  /* the file, held open while this instance is an image */
  int xz;                     /* any chunk is xz */
} instance;

static DIBackingStoreGetDataForkLength_fn get_length;
static DIBackingStoreReadDataFork_fn read_fork;
static DIBackingStoreGetBaseBackingStore_fn get_base;
static DIBackingStoreOpenDataFork_fn open_fork;
static DIBackingStoreCloseDataFork_fn close_fork;
static int resolve(void) {
  if (!get_length) get_length = (DIBackingStoreGetDataForkLength_fn)dlsym(RTLD_DEFAULT, "DIBackingStoreGetDataForkLength");
  if (!read_fork) read_fork = (DIBackingStoreReadDataFork_fn)dlsym(RTLD_DEFAULT, "DIBackingStoreReadDataFork");
  if (!get_base) get_base = (DIBackingStoreGetBaseBackingStore_fn)dlsym(RTLD_DEFAULT, "DIBackingStoreGetBaseBackingStore");
  if (!open_fork) open_fork = (DIBackingStoreOpenDataFork_fn)dlsym(RTLD_DEFAULT, "DIBackingStoreOpenDataFork");
  if (!close_fork) close_fork = (DIBackingStoreCloseDataFork_fn)dlsym(RTLD_DEFAULT, "DIBackingStoreCloseDataFork");
  return get_length && read_fork && get_base && open_fork && close_fork ? 0 : -ENOSYS;
}

static int store_pread(void *ctx, void *buf, size_t len, uint64_t off) {
  unsigned long done = 0;
  int e = read_fork((CBackingStore *)ctx, (long long)off, len, &done, buf);
  if (e || done != len) { TRACE("read %zu at %llu: error %d, %lu read", len, (unsigned long long)off, e, done); return -EIO; }
  return 0;
}
/* platform: the store the framework hands a format is UDIF's encoding layer (its data fork only, the plist and koly
 *           trailer stripped); the base store under it is the file, which is what libudif reads. Opens the file;
 *           the caller closes it with close_fork. */
static int store_reader(CBackingStore *given, udif_reader *r) {
  long long n = 0;
  if (!given || resolve()) { TRACE("store %p: DIBackingStore functions not found", (void *)given); return -EIO; }
  CBackingStore *s = get_base(given);
  if (!s) s = given;
  int e = open_fork(s);
  if (e) { TRACE("store %p (base of %p): open error %d", (void *)s, (void *)given, e); return -EIO; }
  e = get_length(s, &n);
  if (e || n < 0) { close_fork(s); TRACE("store %p: data fork length error %d, length %lld", (void *)s, e, n); return -EIO; }
  r->pread = store_pread; r->size = (uint64_t)n; r->ctx = s;
  return 0;
}

static ULONG add_ref(void *self) { return (ULONG)__sync_add_and_fetch(&((instance *)self)->refs, 1); }
static ULONG release(void *self) {
  instance *in = self;
  int32_t n = __sync_sub_and_fetch(&in->refs, 1);
  if (n == 0) {
    TRACE("release: image %p gone", (void *)in->img);
    udif_image_close(in->img);
    if (in->open_store) close_fork(in->open_store);
    CFPlugInRemoveInstanceForFactory(in->factory); CFRelease(in->factory);
    free(in);
  }
  return (ULONG)n;
}
static HRESULT query_interface(void *self, REFIID iid, LPVOID *out) {
  CFUUIDRef want = CFUUIDCreateFromUUIDBytes(NULL, iid);
  int ok = CFEqual(want, IUnknownUUID) || CFEqual(want, DI_PLUGIN_INTERFACE);
  CFRelease(want);
  if (!ok) { *out = NULL; return E_NOINTERFACE; }
  add_ref(self); *out = self; return S_OK;
}

static int new_probe(CBackingStore *store, CFDictionaryRef options) {
  udif_reader r; udif_table *t = NULL;
  int e = store_reader(store, &r);
  if (e) { TRACE("newProbe: store %p declined: %d", (void *)store, e); return SCORE_DECLINE; }
  e = udif_table_open(&r, &t);
  close_fork((CBackingStore *)r.ctx);
  if (e) { TRACE("newProbe: store %p (%llu bytes) declined: %d", (void *)store, (unsigned long long)r.size, e); return SCORE_DECLINE; }
  int claim = udif_table_has_type(t, 0x80000007) || udif_table_has_type(t, 0x80000008);
  udif_table_close(t);
  TRACE("newProbe: store %p, %s", (void *)store, claim ? "claimed" : "declined");
  return claim ? SCORE_CLAIM : SCORE_DECLINE;
}
static int new_with_backing_store(void *self, CBackingStore *store, CFDictionaryRef options, CCFPlugInDiskImage *wrapper, CFStringRef *error) {
  instance *in = self; udif_reader r;
  int e = store_reader(store, &r);
  if (!e) {
    e = udif_image_open(&r, &in->img);
    if (e) close_fork((CBackingStore *)r.ctx); else in->open_store = r.ctx;
    if (!e) in->xz = udif_image_has_type(in->img, 0x80000008);
  }
  TRACE("newWithBackingStore: store %p, wrapper %p, error %d, %llu sectors", (void *)store, (void *)wrapper, e,
        in->img ? (unsigned long long)(udif_image_size(in->img) / 512) : 0ULL);
  return e ? (e == -ENOMEM ? ENOMEM : EINVAL) : 0;
}
static const char *class_name(void *self) { return "CDMGCodecsDiskImage"; }
/* spec: named as a modern macOS names them: ULMO if any chunk is xz, else ULFO */
static int is_ulmo(const instance *in) { return in->xz; }
static CFStringRef copy_format_description(void *self) {
  return is_ulmo(self) ? CFSTR("UDIF read-only compressed (lzma)") : CFSTR("UDIF read-only compressed (lzfse)");
}
static CFDictionaryRef copy_image_information(void *self) {
  const void *k[] = { CFSTR("Format"), CFSTR("Format Description") };
  const void *v[] = { is_ulmo(self) ? CFSTR("ULMO") : CFSTR("ULFO"), copy_format_description(self) };
  return CFDictionaryCreate(NULL, k, v, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}
static int is_write_enabled(void *self) { return 0; }
static int64_t sector_count(void *self) {
  instance *in = self;
  return in->img ? (int64_t)(udif_image_size(in->img) / 512) : 0;
}
static int read_sectors(void *self, int64_t start, int64_t count, int64_t *done, void *buf) {
  instance *in = self;
  if (done) *done = 0;
  if (!in->img || start < 0 || count < 0) return EINVAL;
  int e = udif_image_read(in->img, buf, (size_t)count * 512, (uint64_t)start * 512);
  TRACE("readSectors %lld+%lld: %d", (long long)start, (long long)count, e);
  if (e) return e == -EINVAL ? EINVAL : EIO;
  if (done) *done = count;
  return 0;
}
static int flush(void *self, unsigned int how) { return 0; }
static DIChecksumNewWith_fn checksum_new;
static DIChecksumSetValue_fn checksum_set;
static int resolve_checksum(void) {
  if (!checksum_new) checksum_new = (DIChecksumNewWith_fn)dlsym(RTLD_DEFAULT, "DIChecksumNewWith");
  if (!checksum_set) checksum_set = (DIChecksumSetValue_fn)dlsym(RTLD_DEFAULT, "DIChecksumSetValue");
  return checksum_new && checksum_set ? 0 : -ENOSYS;
}
static void set_crc(CChecksum *c, uint32_t v) {
  uint8_t be[4] = { v >> 24, v >> 16, v >> 8, v };
  checksum_set(c, be);
}
static CChecksum *copy_image_checksum(void *self) {
  instance *in = self; uint32_t stored = 0;
  if (!in->img || resolve_checksum() || udif_image_stored_crc32(in->img, &stored)) return NULL;
  CChecksum *c = checksum_new(DI_CHECKSUM_CRC32);
  if (c) set_crc(c, stored);
  TRACE("copyImageChecksum: %p, $%08X", (void *)c, stored);
  return c;
}
static int calc_checksum(void *self, CChecksum *c) {
  instance *in = self; uint32_t stored = 0, calculated = 0;
  if (!in->img || !c || resolve_checksum()) return EINVAL;
  int e = udif_image_crc32(in->img, &stored, &calculated);
  TRACE("calcChecksum: %d, $%08X (stored $%08X)", e, calculated, stored);
  if (e) return e == -ENOMEM ? ENOMEM : EIO;
  set_crc(c, calculated);
  return 0;
}

static const DIPlugInInterface VTBL = {
  .QueryInterface = query_interface, .AddRef = add_ref, .Release = release,
  .newProbe = new_probe, .newWithBackingStore = new_with_backing_store,
  .getClassName = class_name, .copyFormatDescriptionString = copy_format_description,
  .copyImageInformation = copy_image_information, .isWriteEnabled = is_write_enabled,
  .getSectorCount = sector_count, .readSectors = read_sectors, .flush = flush,
  .copyImageChecksum = copy_image_checksum, .calcChecksum = calc_checksum,
};

/* the one exported symbol: Info.plist's CFPlugInFactories names it */
__attribute__((visibility("default"))) void *DMGCodecsFactory(CFAllocatorRef allocator, CFUUIDRef type) {
  if (!CFEqual(type, DI_PLUGIN_TYPE)) return NULL;
  instance *in = calloc(1, sizeof *in);
  if (!in) return NULL;
  in->vtbl = &VTBL; in->refs = 1;
  in->factory = CFUUIDCreateFromString(NULL, CFSTR(DC_FACTORY_UUID));
  CFPlugInAddInstanceForFactory(in->factory);
  return in;
}
