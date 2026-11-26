#!/bin/sh
# platform: host-agnostic -- stevedore expands the pkg and clang22's llvm tools read the Mach-O
#   usage: package-pkg.sh     checks dist/dmg-codecs-*.pkg, which build/package-pkg.sh writes; 77 when absent
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
MAVERICKS_ROOT="$R"; export MAVERICKS_ROOT
. "$R/build/lib.sh"
fail() { echo "FAIL: $1"; exit 1; }
set -- "$R"/dist/dmg-codecs-*.pkg
[ -f "$1" ] || { echo "not packaged (no dist/dmg-codecs-*.pkg) -- run build/package-pkg.sh; skipping"; exit 77; }
[ "$#" -eq 1 ] || fail "dist/ holds $# dmg-codecs pkgs; one build makes one"
PKG="$1"
CL="$(sh "$SHIPYARD_SCRIPTS/fetch_clang22.sh" "$R/components/clang22" 2>/dev/null)"
LIPO="$CL/bin/llvm-lipo" OTOOL="$CL/bin/llvm-otool" NM="$CL/bin/llvm-nm" STRINGS="$CL/bin/llvm-strings"
export LIPO OTOOL NM STRINGS
ST="$(sh "$SHIPYARD_SCRIPTS/fetch_stevedore.sh")"
t="$(mktemp -d "${TMPDIR:-/tmp}/dc-package-test.XXXXXX")"; trap 'rm -rf "$t"' EXIT INT TERM
"$ST" expand "$PKG" "$t/x" >/dev/null || fail "stevedore cannot expand $PKG"
mkdir "$t/root"
for pl in "$t"/x/*/Payload; do
  case "$pl" in *dev.mavergreen.base*) continue ;; esac
  gzip -dc "$pl" > "$t/payload.cpio" || fail "cannot decompress $pl"
  ( cd "$t/root" && cpio -id < "$t/payload.cpio" 2>/dev/null ) || fail "cannot unpack $pl"
done

m="$t/root/$DC_TREE/mavergreen.plist"
[ -f "$m" ] || fail "the pkg must install /$DC_TREE/mavergreen.plist"
grep -q "<string>$DC_PLUGIN</string>" "$m" || fail "the manifest's outside must list $DC_PLUGIN, so uninstall removes it"
[ "$("$ST" plist get "$m" identifier)" = dev.mavergreen.dmg-codecs ] || fail "the manifest must name dev.mavergreen.dmg-codecs"

bin="$t/root/$DC_PLUGIN/Contents/MacOS/DMGCodecs"
[ -f "$bin" ] || fail "the pkg must install /$DC_PLUGIN"
extra="$(find "$t/root/$DC_PLUGIN" -name '*.dSYM' | head -1)"
[ -z "$extra" ] || fail "the plug-in bundle must not carry debug symbols (found ${extra#"$t/root/"})"
archs="$("$LIPO" -archs "$bin" | tr -d " ")"; [ "$archs" = x86_64 ] || fail "the plug-in must be x86_64 only (got $archs)"
MAVERICKS_DEVIATIONS_ROOT="$R" MAVERICKS_ALLOW_ARCHS=x86_64 \
  sh "$SHIPYARD_SCRIPTS/assert_binary_compatible.sh" "$bin" >/dev/null || fail "the plug-in must pass the 10.9 compat guard"

app="$t/root/Library/Application Support/Mavergreen/dmg-codecs-updater.app"
[ -d "$app" ] || fail "the pkg must install dmg-codecs-updater.app"
[ -f "$t/root/Library/LaunchAgents/dev.mavergreen.dmg-codecs-updatecheck.plist" ] || fail "the pkg must install the update-check LaunchAgent"
feed="$("$ST" plist get "$app/Contents/Info.plist" SUFeedURL)"
[ "$feed" = https://github.com/Mavergreen/dmg-codecs/releases/latest/download/dmg-codecs.xml ] || fail "the updater must poll dmg-codecs's feed (got $feed)"

d="$t/x/Distribution"
grep -q '<os-version min="10.9.5"' "$d" || fail "the Distribution must declare the 10.9.5 install floor"
first="$(sed -n 's/.*<pkg-ref id="\([^"]*\)".*/\1/p' "$d" | sed -n 1p)"
[ "$first" = dev.mavergreen.base ] || fail "dev.mavergreen.base must be the first component, not $first"
echo "PASS: package"
