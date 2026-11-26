#!/bin/sh
# platform: macOS-only -- hdiutil writes APFS-inside images for the VM kit's apfs step: LZFSE and xz (the plug-in's),
#           and zlib (stock 10.9's own reading, the comparison when linuxfs is absent)
# usage: make-apfs.sh   (rewrites tests/fixtures/apfs-*.dmg)
set -eu
F="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-apfs.XXXXXX")"
trap 'rm -rf "$T"' EXIT
mkdir "$T/src"
i=0; while [ $i -lt 3000 ]; do echo "line $i of an APFS fixture"; i=$((i + 1)); done > "$T/src/text.txt"
for fmt in ULFO ULMO UDZO; do
  n=$(echo "$fmt" | tr 'A-Z' 'a-z')
  hdiutil create -quiet -ov -fs APFS -volname "DC APFS $fmt" -srcfolder "$T/src" -format "$fmt" "$F/apfs-$n.dmg"
done
ls -l "$F"/apfs-*.dmg
