#!/bin/sh
# platform: macOS-only -- a macOS host with the 10.9 VM at `ssh -p 2222 localhost`: every way into the plug-in on 10.9:
#           user and root, convert, imageinfo, verify, over 4 GiB, encryption, and bad images
# usage: paths.sh   (VM_SSH overrides the ssh command; needs passwordless sudo on the VM for the plug-in folder)
set -eu
R="$(cd "$(dirname "$0")/../.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/dmg-codecs-109}"
VM_SSH="${VM_SSH:-ssh -p 2222 localhost}"
W=/var/tmp/dmg-codecs-paths
BUILD_DIR="$B" DC_TRACE="${DC_TRACE:-0}" sh "$R/build/build.sh" plugin
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-paths.XXXXXX")"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a"; cp -R "$B/DMGCodecs.plugin" "$T/a/"; cp "$R"/tests/fixtures/*.dmg "$R/tests/vm/paths-vm.sh" "$T/a/"
python3 -I "$R/tests/vm/mkbad.py" "$R/tests/fixtures" "$T/a"
# spec: tests/vm/paths-vm.sh -- an eject during a copy needs a file that takes seconds to read; 1 GiB that compresses well
mkdir "$T/large"; yes 'a line of the eject image, decoded again and again' | head -c 1073741824 > "$T/large/large.bin"
hdiutil create -quiet -ov -fs HFS+ -volname "DC large" -srcfolder "$T/large" -format ULFO "$T/a/large.dmg"; rm -rf "$T/large"
# platform: 10.9's tar refuses pax headers and xattrs
( cd "$T" && COPYFILE_DISABLE=1 tar --format ustar --no-xattrs --no-mac-metadata -cf - a ) |
  $VM_SSH "rm -rf $W && mkdir -p $W && tar -xf - -C $W && cd $W/a && sh paths-vm.sh" && fail=0 || fail=1
$VM_SSH "cat $W/out/ulfo.cdr 2>/dev/null; rm -rf $W" > "$T/ulfo.cdr"
got="$(python3 -I "$R/tests/fixtures/expected.py" "$R/tests/fixtures/ulfo.dmg" "$T/ulfo.cdr" ulfo 2>/dev/null || true)"
if [ -n "$got" ] && grep -qx "$got" "$R/tests/fixtures/expected.txt"; then echo "ok: UDTO conversion matches expected.txt"
else echo "FAIL: UDTO conversion: got \"$got\""; fail=1; fi
exit $fail
