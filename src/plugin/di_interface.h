/* platform: Mac OS X 10.9.5's DiskImages.framework (DiskImages-373) loads disk-image format plug-ins through this
 *           private CFPlugIn interface. No header was ever published; the layout below is read from the framework's
 *           CCFPlugInDiskImage, which wraps a plug-in: each wrapper method loads one slot, tests it for NULL, and
 *           falls back to CDiskImage's default when it is. Offsets are bytes into the function table.
 *           Every slot left NULL here is one the wrappers test before calling. */
#ifndef DC_DI_INTERFACE_H
#define DC_DI_INTERFACE_H
#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <stdint.h>

/* platform: CCFPlugInDiskImage::newProbe and newWithBackingStore build these with CFUUIDGetConstantUUIDWithBytes */
#define DI_PLUGIN_TYPE      CFUUIDGetConstantUUIDWithBytes(NULL, 0xE3,0xBA,0xA4,0x40,0x86,0xF6,0x11,0xD5,0xBB,0x6C,0x00,0x30,0x65,0x41,0xBA,0x86)
#define DI_PLUGIN_INTERFACE CFUUIDGetConstantUUIDWithBytes(NULL, 0x67,0x66,0xE9,0x4A,0x4D,0x6F,0x12,0x26,0x9E,0x9D,0x00,0x50,0xE4,0xC0,0x00,0x67)

typedef struct CBackingStore CBackingStore;      /* the framework's C++ object; reached only through DIBackingStore* */
typedef struct CCFPlugInDiskImage CCFPlugInDiskImage;

typedef struct DIPlugInInterface {
  IUNKNOWN_C_GUTS;                                                                  /* 0x00 .. 0x18 */
  void *reserved20;                                                                 /* 0x20: never read */
  /* 0x28: called without the instance, for every plug-in, on every attach: > 0 and the highest score wins */
  int (*newProbe)(CBackingStore *store, CFDictionaryRef options);
  /* 0x30: makes this instance the image; the wrapper then keeps it (AddRef) until the image goes */
  int (*newWithBackingStore)(void *self, CBackingStore *store, CFDictionaryRef options, CCFPlugInDiskImage *wrapper, CFStringRef *error);
  void *create[2];                                                                  /* 0x38, 0x40 */
  void *convert[2];                                                                 /* 0x48, 0x50 */
  void *checksumProbe;                                                              /* 0x58 */
  void *reserved60;                                                                 /* 0x60 */
  void *readSectors32;                                                              /* 0x68: unused when 0xd8 is set */
  void *writeSectors32;                                                             /* 0x70 */
  const char *(*getClassName)(void *self);                                          /* 0x78 */
  CFStringRef (*copyFormatDescriptionString)(void *self);                           /* 0x80 */
  CFDictionaryRef (*copyImageInformation)(void *self);                              /* 0x88: merged into CDiskImage's */
  void *getImageFormat;                                                             /* 0x90 */
  void *getSectorCount32;                                                           /* 0x98: unused when 0xd0 is set */
  void *reservedA0;                                                                 /* 0xa0 */
  void *hasValidChecksum, *copyImageChecksum, *calcChecksum, *createUniqueIDDict;  /* 0xa8 .. 0xc0 */
  int (*isWriteEnabled)(void *self);                                                /* 0xc8 */
  int64_t (*getSectorCount)(void *self);                                            /* 0xd0 */
  int (*readSectors)(void *self, int64_t start, int64_t count, int64_t *done, void *buf);  /* 0xd8: 512-byte sectors */
  void *writeSectors;                                                               /* 0xe0 */
  void *handlePowerChange;                                                          /* 0xe8 */
  void *setSectorCount, *maxSectorCount, *validateChecksum;                         /* 0xf0 .. 0x100 */
  int (*flush)(void *self, unsigned int how);                                       /* 0x108: 0, then CDiskImage's own */
} DIPlugInInterface;

/* platform: exported by DiskImages.framework (hdiutil imports them); found with dlsym in the host process, since the
 *           10.9 SDK has no private frameworks to link against */
typedef int (*DIBackingStoreGetDataForkLength_fn)(CBackingStore *, long long *);
typedef CBackingStore *(*DIBackingStoreGetBaseBackingStore_fn)(CBackingStore *);
/* platform: a store must be opened before it is read (else EBADF); CUDIFEncoding::isUDIFFile opens, reads and closes */
typedef int (*DIBackingStoreOpenDataFork_fn)(CBackingStore *);
typedef int (*DIBackingStoreCloseDataFork_fn)(CBackingStore *);
typedef int (*DIBackingStoreReadDataFork_fn)(CBackingStore *, long long off, unsigned long len, unsigned long *done, void *buf);
#endif
