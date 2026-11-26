#!/bin/sh
# platform: macOS-only -- runs ON Mac OS X 10.9 (release.yml's vm-test guest, or the maintainer's VM): installer, hdiutil, sudo
#   usage: installed-smoke.sh <PKG> <VERSION>
#          installs PKG, proves it is VERSION, attaches and verifies ulfo.dmg and ulmo.dmg through it, uninstalls it
#          with the mavergreen helper, and proves 10.9 then refuses ulfo.dmg again. With no arguments, exits 77.
# spec: shipyard check 29 -- a pkg-shipping repo installs its run's own pkg on 10.9 and runs it before publish
set -eu
[ "$#" -eq 2 ] || { echo "installed-smoke: needs <PKG> <VERSION>; only the 10.9 acceptance run passes them; skipping"; exit 77; }
pkg=$1 ver=$2
F="$(cd "$(dirname "$0")" && pwd)/fixtures"
P=/Library/Plug-ins/DiskImages/DMGCodecs.plugin
# platform: the family's base component installs the mavergreen helper at /usr/local/bin, outside the prefix
helper=/usr/local/bin/mavergreen
fail() { echo "FAIL: $*"; exit 1; }
case "$(sw_vers -productVersion)" in 10.9|10.9.*) ;; *) fail "this is not Mac OS X 10.9" ;; esac
[ -f "$pkg" ] || fail "no $pkg"
if pkgutil --pkgs | grep -qx dev.mavergreen.dmg-codecs; then fail "this Mac already has a dmg-codecs receipt"; fi
W="$(mktemp -d /var/tmp/dc-smoke.XXXXXX)"
trap 'for m in "$W"/m-*; do [ -d "$m" ] && hdiutil detach -force "$m" >/dev/null 2>&1; done; rm -rf "$W"' EXIT

sudo installer -pkg "$pkg" -target /
got="$(pkgutil --pkg-info dev.mavergreen.dmg-codecs | awk '$1 == "version:" { print $2 }')"
[ "$got" = "$ver" ] || fail "the installed dev.mavergreen.dmg-codecs is '$got', want $ver"
[ -d "$P" ] || fail "the pkg did not install $P"
"$helper" list | grep -q '^dmg-codecs ' || fail "mavergreen list does not show dmg-codecs"

for img in ulfo ulmo; do
  hdiutil verify "$F/$img.dmg" >/dev/null 2>&1 || fail "hdiutil verify $img.dmg"
  out="$(hdiutil attach -nobrowse -readonly -mountpoint "$W/m-$img" "$F/$img.dmg" 2>&1)" || fail "$img.dmg does not attach: $out"
  grep -q "line 2999 of a fixture" "$W/m-$img/text.txt" || fail "$img.dmg's text.txt does not read back"
  hdiutil detach "$W/m-$img" >/dev/null || fail "$img.dmg does not detach"
  echo "ok: $img.dmg verifies, attaches and reads"
done

sudo "$helper" uninstall dmg-codecs
[ ! -e "$P" ] || fail "uninstall left $P"
if pkgutil --pkgs | grep -qx dev.mavergreen.dmg-codecs; then fail "uninstall left the receipt"; fi
if hdiutil attach -nobrowse -readonly -mountpoint "$W/m-after" "$F/ulfo.dmg" >/dev/null 2>&1; then
  fail "ulfo.dmg still attaches after uninstall"
fi
echo "PASS: installed smoke ($ver)"
