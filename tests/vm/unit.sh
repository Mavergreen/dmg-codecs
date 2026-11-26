#!/bin/sh
# platform: macOS-only -- a macOS host with the 10.9 VM at `ssh -p 2222 localhost`: runs libudif's unit tests ON 10.9
# usage: unit.sh   (VM_SSH overrides the ssh command; the VM stages on its own disk, never the share)
set -eu
R="$(cd "$(dirname "$0")/../.." && pwd)"
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/dmg-codecs-109}"
VM_SSH="${VM_SSH:-ssh -p 2222 localhost}"
BUILD_DIR="$B" sh "$R/build/build.sh" tests
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-unit.XXXXXX")"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/u/fixtures"
for f in "$B"/tests/test_*; do [ -f "$f" ] && cp "$f" "$T/u/"; done; cp "$R"/tests/fixtures/*.dmg "$R/tests/fixtures/expected.txt" "$T/u/fixtures/"
# platform: 10.9's tar refuses pax headers and xattrs
( cd "$T" && COPYFILE_DISABLE=1 tar --format ustar --no-xattrs --no-mac-metadata -cf - u ) |
  $VM_SSH 'rm -rf /var/tmp/dmg-codecs-unit && mkdir -p /var/tmp/dmg-codecs-unit && tar -xf - -C /var/tmp/dmg-codecs-unit &&
           cd /var/tmp/dmg-codecs-unit/u && fail=0 && for t in test_*; do ./$t fixtures || fail=1; done; rm -rf /var/tmp/dmg-codecs-unit; exit $fail'
