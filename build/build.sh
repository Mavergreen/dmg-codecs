#!/bin/sh
# platform: host-agnostic -- cross-builds for Mac OS X 10.9 (x86_64) on a modern Mac or Linux, natively on 10.9
# usage: build.sh [lib|tests]...   (default: lib); out of tree, into BUILD_DIR (default $MAVERICKS_BUILD_ROOT/dmg-codecs-109)
#        lib    BUILD_DIR/libudif.a: libudif and the compiled-in codecs
#        tests  BUILD_DIR/tests/test_*: tests/c against libudif.a, for tests/vm/unit.sh to run on 10.9
#        plugin BUILD_DIR/DMGCodecs.plugin (DC_TRACE=1: every slot call logged to syslog)
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
B="${BUILD_DIR:-$MAVERICKS_BUILD_ROOT/dmg-codecs-109}"; mkdir -p "$B/obj" "$B/tests"
CL="$(sh "$SHIPYARD_SCRIPTS/fetch_clang22.sh" "$R/components/clang22" 2>/dev/null)"
SDK="$(sh "$SHIPYARD_SCRIPTS/fetch_sdk.sh")"
CC="$CL/bin/clang --no-default-config --target=x86_64-apple-macos10.9 -isysroot $SDK"
AR="$CL/bin/llvm-ar"
eval "$(DC_TARGET=Darwin sh "$R/build/codecs.sh" flags)"
lib() {
  objs=""
  for s in $DC_CODEC_SRCS; do
    o="$B/obj/$(basename "$s" .c).o"
    # shellcheck disable=SC2086
    $CC -std=gnu11 -O2 -g -w $DC_CODEC_CFLAGS -c "$s" -o "$o"; objs="$objs $o"
  done
  for s in "$R"/src/*.c; do
    o="$B/obj/$(basename "$s" .c).o"
    # shellcheck disable=SC2086
    $CC -std=c11 -D_DARWIN_C_SOURCE -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter $DC_CODEC_CFLAGS -c "$s" -o "$o"; objs="$objs $o"
  done
  rm -f "$B/libudif.a"
  # shellcheck disable=SC2086
  $AR rcs "$B/libudif.a" $objs
}
tests() {
  [ -f "$B/libudif.a" ] || lib
  for t in "$R"/tests/c/test_*.c; do
    # shellcheck disable=SC2086
    $CC -std=c11 -D_DARWIN_C_SOURCE -O1 -g -Wall -Werror -Wno-unused-parameter -Wno-unused-function -fuse-ld=lld \
      -I"$R/src" $DC_CODEC_CFLAGS "$t" "$B/libudif.a" -lz -lbz2 -o "$B/tests/$(basename "$t" .c)"
  done
}
# platform: CFPlugIn knows a factory by its UUID, so a new one would make 10.9 see a different plug-in
FACTORY=743AFD46-8527-479D-B299-1A28AFB3147B
plugin() {
  lib
  P="$B/DMGCodecs.plugin/Contents"; rm -rf "$B/DMGCodecs.plugin"; mkdir -p "$P/MacOS"
  trace=; [ "${DC_TRACE:-0}" = 1 ] && trace=-DDC_TRACE
  # shellcheck disable=SC2086
  $CC -std=c11 -D_DARWIN_C_SOURCE -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -fvisibility=hidden -fuse-ld=lld -bundle -Wl,-exported_symbol,_DMGCodecsFactory \
    $trace -DDC_FACTORY_UUID="\"$FACTORY\"" -I"$R/src" "$R/src/plugin/plugin.c" "$B/libudif.a" \
    -framework CoreFoundation -lz -lbz2 -o "$P/MacOS/DMGCodecs"
  # platform: clang runs dsymutil when one -g invocation compiles and links, writing the dSYM beside the binary;
  #           it stays in BUILD_DIR for debugging and out of the bundle
  rm -rf "$B/DMGCodecs.dSYM"; if [ -d "$P/MacOS/DMGCodecs.dSYM" ]; then mv "$P/MacOS/DMGCodecs.dSYM" "$B/"; fi
  v="$(sh "$R/build/version.sh" 2>/dev/null || echo 0)"
  sed -e "s/@VERSION@/$v/g" -e "s/@FACTORY@/$FACTORY/g" "$R/src/plugin/Info.plist.in" > "$P/Info.plist"
}
[ $# -gt 0 ] || set -- lib
for a in "$@"; do case $a in lib) lib ;;  tests) lib; tests ;; plugin) plugin ;; *) echo "usage: build.sh [lib|tests|plugin]..." >&2; exit 2 ;; esac; done
