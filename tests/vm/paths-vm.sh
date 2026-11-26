#!/bin/sh
# platform: macOS-only -- runs on Mac OS X 10.9, from tests/vm/paths.sh's staging folder: every way into the plug-in
# usage: sh paths-vm.sh   (cwd holds DMGCodecs.plugin, the fixtures and bad-*.dmg; results go to ../out)
set -u
W="$(cd .. && pwd)"; P=/Library/Plug-ins/DiskImages; O="$W/out"; mkdir -p "$O"
fail=0
ok() { echo "ok: $*"; }
no() { echo "FAIL: $*"; fail=1; }
ours() { hdiutil info | awk -v w="$W" '/^image-path/ {p = index($0, w) > 0} p && /^\/dev\/disk[0-9]+[ \t]/ {print $1}'; }
cleanup() { for d in $(ours); do hdiutil detach -quiet -force "$d"; done; sudo -n rm -rf "$P/DMGCodecs.plugin"; }
trap cleanup EXIT
crashes() { ls "$HOME/Library/Logs/DiagnosticReports" /Library/Logs/DiagnosticReports 2>/dev/null | grep -c -E '^(diskimages-helper|hdiutil)'; }
devs() { hdiutil info | grep -E '^/dev/disk[0-9]+[ \t]' | sort; }
norm() { sed -E -e 's#/dev/disk[0-9]+(s[0-9]+)?#/dev/diskN#g' -e "s#$W#W#g"; }
attach() { i=$1; m=$2; shift 2; hdiutil attach -nobrowse -readonly -mountpoint "$m" "$@" "$i" 2>&1; echo "exit $?"; }

# spec: README.md "What it changes" -- an encrypted image behaves exactly as without the plug-in; see it without first
sudo -n rm -rf "$P/DMGCodecs.plugin" || exit 1
aes_without=$(printf dmg-codecs | attach ulfo-aes.dmg "$W/m-aes" -stdinpass | norm); cleanup
sudo -n mkdir -p "$P" && sudo -n cp -R DMGCodecs.plugin "$P/" && sudo -n chown -R root:wheel "$P/DMGCodecs.plugin" || exit 1
aes_with=$(printf dmg-codecs | attach ulfo-aes.dmg "$W/m-aes" -stdinpass | norm); cleanup
sudo -n mkdir -p "$P" && sudo -n cp -R DMGCodecs.plugin "$P/" && sudo -n chown -R root:wheel "$P/DMGCodecs.plugin" || exit 1
if [ "$aes_with" = "$aes_without" ]; then ok "encrypted ULFO as without the plug-in: $(echo "$aes_with" | tr '\n' ' ')"
else no "encrypted ULFO: without [$aes_without] with [$aes_with]"; fi

base_devs=$(devs); base_crashes=$(crashes)

for who in user root; do
  s=; [ $who = root ] && s="sudo -n"
  out=$($s hdiutil attach -nobrowse -readonly -mountpoint "$W/m-$who" ulfo.dmg 2>&1)
  if [ $? -eq 0 ] && grep -q "line 2999 of a fixture" "$W/m-$who/text.txt"; then ok "attach as $who"; else no "attach as $who: $out"; fi
  $s hdiutil detach -quiet "$W/m-$who" 2>/dev/null
done

if hdiutil convert -quiet ulfo.dmg -format UDZO -o "$O/ulfo-udzo.dmg" && hdiutil verify -quiet "$O/ulfo-udzo.dmg" &&
   [ "$(hdiutil imageinfo "$O/ulfo-udzo.dmg" | awk -F': ' '/^Format:/ {print $2}')" = UDZO ]; then ok "convert to UDZO, which verifies"
else no "convert to UDZO"; fi
if hdiutil convert -quiet ulfo.dmg -format UDTO -o "$O/ulfo" && [ -f "$O/ulfo.cdr" ]; then ok "convert to UDTO (hash checked on the host)"
else no "convert to UDTO"; fi
# spec: README.md "What it changes" -- imageinfo names the format as a modern macOS does
for f in "ulfo ULFO lzfse" "ulmo ULMO lzma"; do
  set -- $f
  out=$(hdiutil imageinfo $1.dmg 2>&1)
  if [ $? -eq 0 ] && echo "$out" | grep -qx "Format: $2" && echo "$out" | grep -qx "Format Description: UDIF read-only compressed ($3)"
  then ok "imageinfo $1: $2 ($3)"; else no "imageinfo $1: $(echo "$out" | head -n 3 | tr '\n' ' ')"; fi
  out=$(hdiutil verify $1.dmg 2>&1) && ok "verify $1" || no "verify $1: $out"
done
out=$(hdiutil verify bad-master.dmg 2>&1) && no "verify bad-master.dmg passed: $out" || ok "verify bad-master.dmg fails: $(echo "$out" | tail -n 1)"

out=$(hdiutil attach -nobrowse -readonly -mountpoint "$W/m-huge" ulfo-huge.dmg 2>&1)
if [ $? -eq 0 ] && grep -q "line 2999 of a fixture" "$W/m-huge/text.txt"; then ok "huge image: a file past 4 GiB reads"; else no "huge image: $out"; fi
hdiutil detach -quiet "$W/m-huge" 2>/dev/null

helper_pids() { ps -axo pid=,command= | awk '/diskimages-helper/ && !/awk/ {print $1}' | sort; }
# spec: README.md "What it changes" -- a damaged image fails with an error: a read error mid-copy is an error, not a hang or a crash
out=$(hdiutil attach -noverify -nobrowse -readonly -mountpoint "$W/m-data" bad-huge-data.dmg 2>&1)
if [ $? -ne 0 ]; then no "bad-huge-data.dmg did not attach with -noverify: $out"
else
  before=$(helper_pids)
  if cp "$W/m-data/text.txt" "$O/text.txt" 2>"$O/cp.err"; then no "copying a file over broken chunks succeeded"
  else ok "copying a file over broken chunks fails: $(tail -n 1 "$O/cp.err")"; fi
  alive=1; for p in $before; do kill -0 "$p" 2>/dev/null || alive=0; done
  [ $alive = 1 ] && ok "the helper serving it keeps running" || no "the helper exited under a read error"
  hdiutil detach -quiet "$W/m-data" && ok "it still detaches" || no "it would not detach after a read error"
fi
[ "$(crashes)" = "$base_crashes" ] || { no "a read error mid-copy crashed hdiutil or the helper"; base_crashes=$(crashes); }
# spec: README.md "What it changes" -- an eject during a large copy ends the copy with an error and leaves nothing attached
out=$(hdiutil attach -nobrowse -readonly -mountpoint "$W/m-large" large.dmg 2>&1)
if [ $? -ne 0 ]; then no "large.dmg did not attach: $out"
else
  cat "$W/m-large/large.bin" > /dev/null 2>"$O/cat.err" & cp_pid=$!
  sleep 1
  if kill -0 $cp_pid 2>/dev/null; then
    hdiutil detach -force "$W/m-large" >/dev/null 2>&1 && ok "a forced eject lands during a 1 GiB copy" || no "the forced eject was refused"
    wait $cp_pid; rc=$?
    [ $rc -ne 0 ] && ok "the copy ends with an error (cat $rc: $(tail -n 1 "$O/cat.err"))" || no "the copy reported success after the eject"
  else wait $cp_pid; no "the copy finished before the eject; make large.bin bigger"; hdiutil detach -quiet -force "$W/m-large"; fi
fi
[ "$(devs)" = "$base_devs" ] || { no "the eject left something attached"; for d in $(ours); do hdiutil detach -quiet -force "$d"; done; }
[ "$(crashes)" = "$base_crashes" ] || { no "an eject mid-read crashed hdiutil or the helper"; base_crashes=$(crashes); }

for b in bad-*.dmg; do
  [ "$b" = bad-huge-data.dmg ] && continue
  r=$(attach "$b" "$W/m-bad" | tail -n 2 | tr '\n' ' ')
  case "$r" in *"exit 0"*) no "$b attached: $r"; hdiutil detach -quiet -force "$W/m-bad" ;; *) ;; esac
  if [ "$(devs)" != "$base_devs" ]; then no "$b left something attached"; for d in $(ours); do hdiutil detach -quiet -force "$d"; done
  elif [ "$(crashes)" != "$base_crashes" ]; then no "$b crashed hdiutil or the helper"; base_crashes=$(crashes)
  else ok "$b declined cleanly: $r"; fi
done
exit $fail
