# DMG Codecs for Mavericks

**This README has not been read or edited by a human yet.** Until it has, this project cannot cut
its first release.

Mac OS X 10.9 Mavericks says this when you open many of today's downloads:

> The following disk images couldn't be opened … corrupt image

    hdiutil: attach failed - corrupt image

The disk image is fine. It is compressed with LZFSE or LZMA (xz), which Apple added after 10.9.
DMG Codecs teaches 10.9 both, so those disk images open the way any other does: double-click,
`hdiutil attach`, `hdiutil convert`. It changes nothing else.

An unofficial community project, not affiliated with Apple.

## What it opens

The disk images many apps ship today, among them Firefox, Thunderbird, Chrome, Brave, Docker and OBS.
calibre's disk image also holds an APFS volume, which 10.9 cannot read by itself; with
[linuxfs](https://github.com/Mavergreen/linuxfs) installed, it opens too.

## Install

Download `dmg-codecs-<version>.pkg` from the [latest release](https://github.com/Mavergreen/dmg-codecs/releases/latest)
and open it. It needs Mac OS X 10.9.5. Nothing to restart: the next disk image you open uses it. An updater
checks for new versions once a day.

## What it changes

- Disk images compressed with LZFSE (`ULFO`) or LZMA (`ULMO`) attach, convert, verify and describe themselves
  (`hdiutil imageinfo` reports the format a modern macOS would).
- Every other disk image is opened by 10.9 exactly as before, encrypted ones included.
- A damaged disk image fails with an error, as 10.9 fails on any damaged image. Nothing is left attached.

## How it works

10.9's DiskImages framework loads plug-ins from `/Library/Plug-ins/DiskImages`. DMG Codecs is one: when a disk image
has LZFSE or LZMA chunks, it claims the image and decompresses those chunks as they are read. It uses Apple's own
LZFSE decoder and the xz-embedded LZMA decoder, compiled in (see [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)).
It is read-only, and it never writes to the disk image.

## Uninstall

    sudo mavergreen uninstall dmg-codecs

That removes the plug-in, its updater and its folder in `/usr/local/mavergreen`. LZFSE and LZMA disk images then
fail to open again, as they did before.

## Building

`sh build/package-pkg.sh` builds the plug-in, its updater and `dist/dmg-codecs-<version>.pkg`, on a modern Mac, on
Linux or on 10.9, with [shipyard](https://github.com/Mavergreen/shipyard) installed. `sh tests/c/run.sh` runs the
decoder's unit tests on the host. `tests/vm/` holds the tests that run on a 10.9 virtual machine.
