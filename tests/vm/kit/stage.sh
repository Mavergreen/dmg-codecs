#!/bin/sh
# platform: macOS-only -- a macOS host with the 10.9 VM at `ssh -p 2222 localhost`: stages the VM kit and fetches its results
# usage: stage.sh [PKG]   builds the plug-in, copies run.sh, the fixtures, a stress image and (if given) the product's
#                         .pkg to the VM's /var/tmp/dc-kit; then on the VM: sh /var/tmp/dc-kit/run.sh STEP...
#        stage.sh fetch   copies /var/tmp/dc-kit-out.tar.gz back into .superpowers/kits/dmg-codecs-results/
set -eu
R="$(cd "$(dirname "$0")/../../.." && pwd)"
. "$R/build/lib.sh"
VM_SSH="${VM_SSH:-ssh -p 2222 localhost}"
if [ "${1:-}" = fetch ]; then
  D="$R/.superpowers/kits/dmg-codecs-results"; mkdir -p "$D"
  $VM_SSH 'cat /var/tmp/dc-kit-out.tar.gz' > "$D/dc-kit-out.tar.gz"
  tar -xzf "$D/dc-kit-out.tar.gz" -C "$D" && echo "results: $D" && cat "$D"/dc-kit-out/verdicts.txt
  exit 0
fi
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/dmg-codecs-109}"
BUILD_DIR="$B" sh "$R/build/build.sh" plugin
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-kit.XXXXXX")"; trap 'rm -rf "$T"' EXIT
K="$T/dc-kit"; mkdir -p "$K/src"
cp "$R/tests/vm/kit/run.sh" "$R/tests/vm/kit/README.md" "$K/"; cp -R "$B/DMGCodecs.plugin" "$K/"
cp "$R"/tests/fixtures/ulfo.dmg "$R"/tests/fixtures/ulmo.dmg "$R"/tests/fixtures/apfs-*.dmg "$K/"
if [ -n "${1:-}" ]; then mkdir -p "$K/pkg"; cp "$1" "$K/pkg/"; fi
# spec: tests/vm/kit/README.md -- a copy interrupted by an eject needs a large file: 2 GiB that compresses well, so the dmg stays small
yes 'a line of the stress image, decoded again and again' | head -c 2147483648 > "$K/src/large.bin"
hdiutil create -quiet -ov -fs HFS+ -size 3g -volname "DC stress" -srcfolder "$K/src" -format ULFO "$K/stress.dmg"
rm -rf "$K/src"
git -C "$R" rev-parse HEAD > "$K/COMMIT"
( cd "$K" && find . -type f | sort | xargs shasum -a 256 > "$T/SHA256SUMS" ) && mv "$T/SHA256SUMS" "$K/"
# platform: 10.9's tar refuses pax headers and xattrs
( cd "$T" && COPYFILE_DISABLE=1 tar --format ustar --no-xattrs --no-mac-metadata -cf - dc-kit ) |
  $VM_SSH 'rm -rf /var/tmp/dc-kit && tar -xf - -C /var/tmp && echo "staged /var/tmp/dc-kit ($(cat /var/tmp/dc-kit/COMMIT))"'
