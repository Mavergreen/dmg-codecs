#!/bin/sh
# platform: host-agnostic
# usage: codecs.sh fetch DIR     fetches every codec source at its pin (components/<name>/{repo,version,sha256}) into DIR/<name>
#        codecs.sh flags         prints shell assignments DC_CODEC_CFLAGS, DC_CODEC_SRCS and DC_CODEC_LIBS for a host build,
#                                fetching into $MAVERICKS_BUILD_ROOT/dmg-codecs-src first if needed
# platform: 10.9 and macOS ship libz and libbz2 (the plug-in links 10.9's own); a Linux runner need not have their headers,
#           so Linux builds them from pinned sources, while lzfse and xz-embedded are compiled in everywhere
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
C="${DC_COMPONENTS:-$R/components}"
fetch() {
  mkdir -p "$1"
  for n in lzfse xz-embedded zlib bzip2; do
    sh "$SHIPYARD_SCRIPTS/fetch_pinned_source.sh" "$(cat "$C/$n/repo")" "$(cat "$C/$n/version")" "$(cat "$C/$n/sha256")" "$1/$n" >&2
  done
}
case "${1:-}" in
  fetch) [ $# -eq 2 ] || { echo "usage: codecs.sh fetch DIR" >&2; exit 2; }; fetch "$2" ;;
  flags)
    S="$MAVERICKS_BUILD_ROOT/dmg-codecs-src"
    v="$(cat "$C"/*/version "$C"/*/sha256 | cksum | cut -d' ' -f1)"
    [ "$(cat "$S/.pins" 2>/dev/null || :)" = "$v" ] || { fetch "$S"; echo "$v" > "$S/.pins"; }
    L="$S/lzfse/src"; X="$S/xz-embedded"
    cflags="-I$L -I$X/linux/include/linux -I$X/userspace -DXZ_USE_CRC64"
    srcs="$L/lzfse_decode.c $L/lzfse_decode_base.c $L/lzfse_fse.c $L/lzvn_decode_base.c"
    srcs="$srcs $X/linux/lib/xz/xz_crc32.c $X/linux/lib/xz/xz_crc64.c $X/linux/lib/xz/xz_dec_stream.c $X/linux/lib/xz/xz_dec_lzma2.c"
    case "${DC_TARGET:-$(uname -s)}" in
      Linux)
        # platform: bzip2's master leaves bz_version.h to meson; write it from the template and meson.build's version
        G="$S/bzip2-gen"; mkdir -p "$G"
        bv="$(sed -n "s/^ *version *: *'\\([^']*\\)'.*/\\1/p" "$S/bzip2/meson.build" | head -n 1)"
        [ -n "$bv" ] || { echo "codecs.sh: no version in bzip2's meson.build" >&2; exit 1; }
        sed "s/@BZ_VERSION@/$bv/" "$S/bzip2/bz_version.h.in" > "$G/bz_version.h"
        cflags="$cflags -I$S/zlib -I$S/bzip2 -I$G"
        for f in adler32 crc32 inflate inftrees inffast zutil uncompr compress deflate trees; do srcs="$srcs $S/zlib/$f.c"; done
        for f in bzlib blocksort compress decompress huffman crctable randtable; do srcs="$srcs $S/bzip2/$f.c"; done
        libs="" ;;
      *) libs="-lz -lbz2" ;;
    esac
    printf "DC_CODEC_CFLAGS='%s'\nDC_CODEC_SRCS='%s'\nDC_CODEC_LIBS='%s'\n" "$cflags" "$srcs" "$libs" ;;
  *) echo "usage: codecs.sh fetch DIR | flags" >&2; exit 2 ;;
esac
