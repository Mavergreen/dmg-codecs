#!/bin/sh
# platform: macOS-only -- hdiutil writes the fixtures; only a modern macOS writes ULFO (10.11+) and ULMO (10.15+)
# usage: make-fixtures.sh   (rewrites tests/fixtures/*.dmg and expected.txt)
set -eu
F="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-fixtures.XXXXXX")"
trap 'rm -rf "$T"' EXIT
mkdir "$T/src"
i=0; while [ $i -lt 3000 ]; do echo "line $i of a fixture that compresses well"; i=$((i + 1)); done > "$T/src/text.txt"
# platform: hdiutil stores a chunk that does not compress raw (type 1), beside the compressed ones
head -c 65536 /dev/urandom > "$T/src/random.bin"
: > "$T/src/empty"
for fmt in ULFO ULMO UDZO UDBZ UDRO; do
  n=$(echo "$fmt" | tr 'A-Z' 'a-z')
  hdiutil create -quiet -ov -fs HFS+ -volname "DC $fmt" -srcfolder "$T/src" -format "$fmt" "$F/$n.dmg"
done
hdiutil create -quiet -ov -fs HFS+ -volname "DC huge" -size 5g -layout NONE "$T/huge.dmg"
hdiutil attach -quiet -nobrowse -mountpoint "$T/m" "$T/huge.dmg"
# spec: tests/vm/paths-vm.sh -- reads text.txt past 4 GiB to prove 64-bit sector offsets; the filler takes the low blocks
dd if=/dev/zero of="$T/m/filler" bs=1048576 count=4300 2>/dev/null
cp "$T/src/text.txt" "$T/m/"
rm "$T/m/filler"
hdiutil detach -quiet "$T/m"
hdiutil convert -quiet -ov "$T/huge.dmg" -format ULFO -o "$F/ulfo-huge.dmg"
# spec: tests/vm/paths-vm.sh -- the encrypted image must behave as without the plug-in; its password is a fixture, not a secret
printf 'dmg-codecs' | hdiutil create -quiet -ov -fs HFS+ -volname "DC aes" -srcfolder "$T/src" -format ULFO -encryption AES-128 -stdinpass "$F/ulfo-aes.dmg"
: > "$F/expected.txt"
for d in ulfo ulmo udzo udbz udro ulfo-huge; do
  hdiutil convert -quiet -ov "$F/$d.dmg" -format UDTO -o "$T/$d"
  python3 -I "$F/expected.py" "$F/$d.dmg" "$T/$d.cdr" "$d" >> "$F/expected.txt"
done
python3 -I -c 'import mmap, sys
f = open(sys.argv[1], "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
i = m.find(b"line 2999 of a fixture"); assert i > 4 << 30, i' "$T/ulfo-huge.cdr"
ls -l "$F"/*.dmg
