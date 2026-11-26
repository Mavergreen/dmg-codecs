#!/bin/sh
# platform: macOS-only -- a macOS host with the 10.9 VM at `ssh -p 2222 localhost`: the plug-in attaches LZFSE and xz images on 10.9
# usage: attach.sh   (VM_SSH overrides the ssh command; needs passwordless sudo on the VM for the plug-in folder)
#        The plug-in is installed into /Library/Plug-ins/DiskImages for the run and removed after, whatever happens.
set -eu
R="$(cd "$(dirname "$0")/../.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/dmg-codecs-109}"
VM_SSH="${VM_SSH:-ssh -p 2222 localhost}"
BUILD_DIR="$B" DC_TRACE=1 sh "$R/build/build.sh" plugin
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-attach.XXXXXX")"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/a"; cp -R "$B/DMGCodecs.plugin" "$T/a/"; cp "$R"/tests/fixtures/*.dmg "$T/a/"
# platform: 10.9's tar refuses pax headers and xattrs
( cd "$T" && COPYFILE_DISABLE=1 tar --format ustar --no-xattrs --no-mac-metadata -cf - a ) | $VM_SSH '
  set -u; W=/var/tmp/dmg-codecs-attach; P=/Library/Plug-ins/DiskImages
  rm -rf $W && mkdir -p $W && tar -xf - -C $W && cd $W/a || exit 1
  cleanup() { for d in $(hdiutil info | awk "/^\/dev\/disk[0-9]+[[:space:]]/ && /DC / {print \$1}"); do hdiutil detach -quiet -force $d; done
              sudo -n rm -rf $P/DMGCodecs.plugin; rm -rf $W; }
  trap cleanup EXIT
  sudo -n mkdir -p $P && sudo -n rm -rf $P/DMGCodecs.plugin && sudo -n cp -R DMGCodecs.plugin $P/ && sudo -n chown -R root:wheel $P/DMGCodecs.plugin || exit 1
  fail=0
  for img in ulfo ulmo; do
    out=$(hdiutil attach -nobrowse -readonly -mountpoint $W/m-$img $img.dmg 2>&1) || { echo "FAIL: $img: $out"; fail=1; continue; }
    if grep -q "line 2999 of a fixture" $W/m-$img/text.txt 2>/dev/null && test "$(wc -c < $W/m-$img/random.bin)" -eq 65536; then echo "ok: $img attaches and reads"; else echo "FAIL: $img contents"; fail=1; fi
    hdiutil detach -quiet $W/m-$img || fail=1
  done
  fmt=$(hdiutil imageinfo udzo.dmg 2>&1 | awk -F": " "/^Format:/ {print \$2}")
  if test "$fmt" = UDZO; then echo "ok: udzo stays stock ($fmt)"; else echo "FAIL: udzo imageinfo format \"$fmt\""; fail=1; fi
  syslog -k Message S DMGCodecs 2>/dev/null | tail -n 40; exit $fail'
