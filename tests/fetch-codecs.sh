#!/bin/sh
# platform: host-agnostic -- build/codecs.sh fetches every codec at its pin, and refuses a source whose SHA-256 is not the pinned one
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d "${TMPDIR:-/tmp}/dc-fetch.XXXXXX")"; trap 'rm -rf "$T"' EXIT
fail=0
sh "$R/build/codecs.sh" fetch "$T/ok" > /dev/null 2>&1 || { echo "FAIL: fetch at the pins failed" >&2; fail=1; }
for f in lzfse/src/lzfse_decode.c xz-embedded/linux/lib/xz/xz_dec_stream.c zlib/inflate.c bzip2/decompress.c; do
  [ -f "$T/ok/$f" ] || { echo "FAIL: $f not fetched" >&2; fail=1; }
done
cp -R "$R/components" "$T/components"
echo 0000000000000000000000000000000000000000000000000000000000000000 > "$T/components/lzfse/sha256"
if DC_COMPONENTS="$T/components" sh "$R/build/codecs.sh" fetch "$T/bad" > /dev/null 2>&1; then echo "FAIL: a wrong pin was accepted" >&2; fail=1; fi
[ "$fail" -eq 0 ] && echo "fetch-codecs OK"
exit "$fail"
