#!/bin/sh
# platform: macOS-only -- hdiutil writes the fixtures; only a modern macOS writes ULFO (10.11+) and ULMO (10.15+)
# usage: make-fixtures.sh   (rewrites tests/fixtures/*.dmg and expected.txt)
set -eu
F="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-fixtures.XXXXXX")"
trap 'rm -rf "$T"' EXIT
mkdir "$T/src"
i=0; while [ $i -lt 3000 ]; do echo "line $i of a fixture that compresses well"; i=$((i + 1)); done > "$T/src/text.txt"
# spec: some data that does not compress, so a chunk is stored raw (type 1) beside compressed ones
head -c 65536 /dev/urandom > "$T/src/random.bin"
: > "$T/src/empty"
for fmt in ULFO ULMO UDZO UDBZ UDRO; do
  n=$(echo "$fmt" | tr 'A-Z' 'a-z')
  hdiutil create -quiet -ov -fs HFS+ -volname "DC $fmt" -srcfolder "$T/src" -format "$fmt" "$F/$n.dmg"
done
# spec: over 4 GiB of sectors, mostly zero runs, so the dmg stays small
hdiutil create -quiet -ov -fs HFS+ -volname "DC huge" -size 5g -layout NONE "$T/huge.dmg"
hdiutil attach -quiet -nobrowse -mountpoint "$T/m" "$T/huge.dmg"
cp "$T/src/text.txt" "$T/m/"
hdiutil detach -quiet "$T/m"
hdiutil convert -quiet -ov "$T/huge.dmg" -format ULFO -o "$F/ulfo-huge.dmg"
: > "$F/expected.txt"
for d in ulfo ulmo udzo udbz udro ulfo-huge; do
  hdiutil convert -quiet -ov "$F/$d.dmg" -format UDTO -o "$T/$d"
  python3 -I "$F/expected.py" "$F/$d.dmg" "$T/$d.cdr" "$d" >> "$F/expected.txt"
done
ls -l "$F"/*.dmg
