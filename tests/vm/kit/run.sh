#!/bin/sh
# platform: macOS-only -- Mac OS X 10.9 with a console user logged in and passwordless sudo for this run; staged by stage.sh
# usage: sh run.sh STEP...   as the console user (root re-runs it as that user); steps run in the order given.
#        what only a person's 10.9 session shows -- double-click, APFS inside, stress, uninstall
#   1 preflight (read only)    2 install (pkg/*.pkg if staged, else the bare plug-in) + the Accessibility grant
#   3 double-click             4 apfs            5 stress            6 uninstall
#   8 clean up                 9 report (OUT as /var/tmp/dc-kit-out.tar.gz)
#   knobs: KIT_WAIT (s to wait for a mount, default 120).
KIT=$(cd "$(dirname "$0")" && pwd)
OUT=/var/tmp/dc-kit-out; WK=/var/tmp/dc-kit-work; P=/Library/Plug-ins/DiskImages/DMGCodecs.plugin
WAIT=${KIT_WAIT:-120}

console_user() { stat -f %Su /dev/console; }
if test "$(id -u)" = 0; then
  cu=$(console_user); case $cu in root|"") echo "STOP: nobody is logged in at the console"; exit 1 ;; esac
  exec sudo -u "$cu" -H env ${KIT_WAIT:+KIT_WAIT=$KIT_WAIT} /bin/sh "$KIT/run.sh" "$@"
fi
mkdir -p "$OUT" "$WK"
ts() { date '+%Y-%m-%d %H:%M:%S'; }
say() { echo "$(ts) $*" | tee -a "$OUT/progress.log"; }
verdict() { echo "$S: $*" >> "$OUT/verdicts.txt"; say "VERDICT $S: $*"; }
# platform: 10.9 has no timeout(1), so wd SECONDS CMD... runs CMD under a watchdog; 124 when killed
wd() {
  wt=$1; shift; "$@" & wp=$!
  ( k=0; while test $k -lt "$wt"; do sleep 1; k=$((k + 1)); done; kill -9 "$wp" 2> /dev/null ) < /dev/null > /dev/null 2>&1 & ww=$!
  wait "$wp"; wrc=$?; kill "$ww" 2> /dev/null; wait "$ww" 2> /dev/null
  test $wrc -ne 137 || wrc=124; return $wrc
}
lw_pid() { ps -axo pid=,user=,command= | awk -v u="$(console_user)" '$2 == u && $3 ~ /loginwindow$/ { print $1; exit }'; }
aqua() { sudo -n launchctl bsexec "$(lw_pid)" sudo -n -u "$(console_user)" "$@"; }
shot() { mkdir -p "$OUT/shots"; wd 60 aqua screencapture -x -t jpg "$OUT/shots/$1.jpg" > /dev/null 2>&1 || say "screenshot $1 failed"; }
osa() { n=$#; while test $n -gt 0; do set -- "$@" -e "$1"; shift; n=$((n - 1)); done; wd 60 aqua osascript "$@" 2>&1 < /dev/null; }
dialogs() {
  osa "tell application \"System Events\" to tell process \"$1\"" 'set r to ""' 'repeat with w in windows' \
    'set r to r & "buttons: "' 'repeat with b in buttons of w' 'set r to r & (name of b as text) & "; "' 'end repeat' \
    'set r to r & " texts: "' 'repeat with t in static texts of w' 'set r to r & (value of t as text) & "; "' 'end repeat' \
    'try' 'repeat with rw in rows of table 1 of scroll area 1 of w' 'repeat with c in UI elements of rw' \
    'try' 'set r to r & (value of c as text) & "; "' 'end try' \
    'try' 'repeat with x in UI elements of c' 'set r to r & (value of x as text) & "; "' 'end repeat' 'end try' \
    'end repeat' 'end repeat' 'end try' \
    'end repeat' 'return r' 'end tell'
}
click() {
  osa "tell application \"System Events\" to tell process \"$1\"" 'repeat with w in windows' 'repeat with b in buttons of w' \
    "if (name of b as text) starts with \"$2\" then" 'click b' 'return "clicked"' 'end if' 'end repeat' 'end repeat' 'return "none"' 'end tell'
}
TCCDB="/Library/Application Support/com.apple.TCC/TCC.db"
tcc_grant() {
  for c in /usr/bin/osascript:1 com.apple.systemevents:0; do
    cl=${c%:*} ct=${c##*:}
    if test -z "$(sudo -n sqlite3 "$TCCDB" "SELECT allowed FROM access WHERE service = 'kTCCServiceAccessibility' AND client = '$cl'")"; then
      sudo -n sqlite3 "$TCCDB" "INSERT INTO access (service, client, client_type, allowed, prompt_count) VALUES ('kTCCServiceAccessibility', '$cl', $ct, 1, 1)" &&
        echo "$cl" >> "$OUT/tcc-added" && say "Accessibility granted to $cl (step 8 takes it away)"
    fi
  done
}
tcc_revoke() {
  test -s "$OUT/tcc-added" || return 0
  while read -r cl; do sudo -n sqlite3 "$TCCDB" "DELETE FROM access WHERE service = 'kTCCServiceAccessibility' AND client = '$cl'"; done < "$OUT/tcc-added"
  rm -f "$OUT/tcc-added"
}
crashes() { ls "$HOME/Library/Logs/DiagnosticReports" /Library/Logs/DiagnosticReports 2> /dev/null | grep -c -E '^(diskimages-helper|hdiutil|DiskImageMounter)'; }
ours() { hdiutil info | awk -v k="$KIT" -v w="$WK" '/^image-path/ { p = index($0, k) || index($0, w) } p && /^\/dev\/disk[0-9]+[ \t]/ { print $1 }'; }
detach_all() { for d in $(ours); do wd 120 hdiutil detach -force "$d" > /dev/null 2>&1; done; }
mounted_from() { mount | awk -v d="$1" 'index($1, d "s") == 1 { sub(/^[^ ]+ on /, ""); sub(/ \([^)]*\)$/, ""); print; exit }'; }
wait_mount() { k=0; while test $k -lt "$WAIT"; do m=$(mounted_from "$1"); test -z "$m" || { echo "$m"; return 0; }; sleep 2; k=$((k + 2)); done; return 1; }
# platform: a FUSE mount does not name /dev/diskNsM as its source, so take the mount point hdiutil printed
mp_of_attach() { awk -F'\t' '/^\/dev\/disk/ && $NF ~ /^\// { print $NF; exit }' "$OUT/attach.tmp"; }
wait_mp() { k=0; while test $k -lt "$WAIT"; do mount | grep -qF " on $1 (" && return 0; sleep 2; k=$((k + 2)); done; return 1; }
attach() { wd 300 hdiutil attach -readonly "$@" > "$OUT/attach.tmp" 2>&1; r=$?; cat "$OUT/attach.tmp" >> "$OUT/$S.log"; awk '/^\/dev\/disk[0-9]+[ \t]/ { print $1; exit }' "$OUT/attach.tmp"; return $r; }
installed() { test -d "$P"; }
linuxfs() { test -d /usr/local/mavergreen/linuxfs; }

preflight() {
  S=preflight
  verdict "$(sw_vers -productVersion) $(sw_vers -buildVersion), console user $(console_user), kit $(cat "$KIT/COMMIT" 2> /dev/null)"
  sudo -n true || { verdict "STOP: passwordless sudo is needed for this run"; exit 1; }
  test -n "$(lw_pid)" || { verdict "STOP: no loginwindow for the console user"; exit 1; }
  (cd "$KIT" && shasum -a 256 -c SHA256SUMS > "$OUT/sums.log" 2>&1) && verdict "kit files match SHA256SUMS" || verdict "STOP: kit files differ from SHA256SUMS"
  verdict "plug-in installed already: $(installed && echo yes || echo no); linuxfs: $(linuxfs && echo yes || echo no); macFUSE: $(kextstat 2> /dev/null | grep -c -i fuse | tr -d ' ') kext(s) loaded"
}
install() {
  S=install
  if ls "$KIT"/pkg/*.pkg > /dev/null 2>&1; then
    for p in "$KIT"/pkg/*.pkg; do wd 600 sudo -n installer -pkg "$p" -target / >> "$OUT/$S.log" 2>&1 && verdict "installed $(basename "$p")" || verdict "FAIL: installer $(basename "$p")"; done
  else
    sudo -n mkdir -p "$(dirname "$P")" && sudo -n rm -rf "$P" && sudo -n cp -R "$KIT/DMGCodecs.plugin" "$P" && sudo -n chown -R root:wheel "$P" &&
      verdict "bare plug-in copied to $P (no pkg staged: step 6 then removes it by hand)" || verdict "FAIL: plug-in copy"
  fi
  tcc_grant
}
doubleclick() {
  S=double-click
  for n in ulfo ulmo; do
    cp "$KIT/$n.dmg" "$WK/"
    aqua open "$WK/$n.dmg"; k=0; d=; while test -z "$d" && test $k -lt "$WAIT"; do sleep 2; k=$((k + 2)); d=$(ours | head -n 1); done
    m=; test -z "$d" || m=$(wait_mount "$d")
    sleep 3; shot "$n-double-click"
    if test -n "$m" && grep -q "line 2999 of a fixture" "$m/text.txt"; then verdict "$n: opened by double-click, mounted at $m, text.txt reads"
    else verdict "FAIL: $n: double-click gave disk '$d' mount '$m'; DiskImageMounter: $(dialogs DiskImageMounter)"; click DiskImageMounter OK > /dev/null; fi
    detach_all
  done
  installed || { verdict "FAIL: the plug-in is not installed (step 2 first)"; return; }
  sudo -n mv "$P" "$WK/aside.plugin"
  aqua open "$WK/ulfo.dmg"; sleep 15
  t=$(dialogs DiskImageMounter); shot ulfo-without-plugin
  case $t in *orrupt*) verdict "without the plug-in: DiskImageMounter says: $t" ;; *) verdict "FAIL: without the plug-in, expected \"corrupt image\": $t" ;; esac
  verdict "dialog dismissed: $(click DiskImageMounter OK)"; detach_all
  sudo -n mv "$WK/aside.plugin" "$P"
}
apfs() {
  S=apfs
  if linuxfs; then
    # spec: tests/vm/kit/README.md step 4 -- the release gate: with linuxfs, an LZFSE and an xz image with APFS inside mount read-only and eject cleanly
    for n in ulfo ulmo; do
      d=$(attach "$KIT/apfs-$n.dmg"); m=$(mp_of_attach); test -z "$m" || wait_mp "$m" || m=
      if test -n "$m" && grep -q "line 2999 of an APFS fixture" "$m/text.txt" && mount | grep -F " on $m (" | grep -q read-only; then
        wd 120 hdiutil detach "$d" >> "$OUT/$S.log" 2>&1 && verdict "apfs-$n: mounted read-only at $m through linuxfs, ejected cleanly" ||
          { verdict "FAIL: apfs-$n: mounted, but did not eject cleanly"; detach_all; }
      else verdict "FAIL: apfs-$n: disk '$d' mount '$m' ($(mount | grep -F " on $m (" ))"; detach_all; fi
    done
  else
    # spec: tests/vm/kit/README.md step 4 -- without linuxfs, the plug-in's APFS images fail no worse than stock 10.9 does with a zlib one
    for n in udzo ulfo ulmo; do
      attach "$KIT/apfs-$n.dmg" > /dev/null; r=$?; sleep 10
      eval "r_$n=\"exit $r, $(tail -n 3 "$OUT/attach.tmp" | sed -E 's#/dev/disk[0-9]+(s[0-9]+)?#/dev/diskN#g' | tr '\n\t' '  ')\""
      detach_all
    done
    verdict "without linuxfs: stock zlib APFS: $r_udzo"
    for n in ulfo ulmo; do eval "v=\$r_$n"; test "$v" = "$r_udzo" && verdict "apfs-$n as stock zlib APFS: $v" || verdict "FAIL: apfs-$n: $v"; done
  fi
}
stress() {
  S=stress; c0=$(crashes)
  a=$(attach -nobrowse -mountpoint "$WK/m-ulfo" "$KIT/ulfo.dmg"); b=$(attach -nobrowse -mountpoint "$WK/m-ulmo" "$KIT/ulmo.dmg")
  if grep -q "line 2999" "$WK/m-ulfo/text.txt" && grep -q "line 2999" "$WK/m-ulmo/text.txt"; then verdict "two images attached at once ($a, $b), both read"
  else verdict "FAIL: two at once: '$a' '$b'"; fi
  detach_all
  # spec: tests/vm/kit/README.md -- a copy interrupted by an eject
  d=$(attach "$KIT/stress.dmg"); m=; test -z "$d" || m=$(wait_mount "$d")
  test -n "$m" || { verdict "FAIL: stress image did not mount ($d)"; detach_all; return; }
  rm -rf "$WK/copy"; mkdir -p "$WK/copy"
  osa "tell application \"Finder\" to duplicate (POSIX file \"$m/large.bin\" as alias) to (POSIX file \"$WK/copy\" as alias)" > "$OUT/stress-copy.osa" &
  cp_pid=$!
  # spec: tests/vm/kit/README.md -- the eject lands mid-copy: as soon as the copy has written something, well before its 2 GiB
  k=0; while test $k -lt 120 && test "$(stat -f %z "$WK/copy/large.bin" 2> /dev/null || echo 0)" -lt 1048576; do sleep 1; k=$((k + 1)); done
  sz=$(stat -f %z "$WK/copy/large.bin" 2> /dev/null || echo 0); shot stress-copying
  wd 120 hdiutil detach "$d" > "$OUT/stress-eject.log" 2>&1; r=$?
  verdict "the eject came with $sz bytes of 2147483648 copied"
  sleep 3; shot stress-after-eject
  verdict "eject during the copy: exit $r, $(tail -n 1 "$OUT/stress-eject.log"); Finder: $(dialogs Finder)"
  click Finder OK > /dev/null; click Finder Stop > /dev/null; wait "$cp_pid" 2> /dev/null
  detach_all
  d=$(attach -nobrowse -mountpoint "$WK/m-after" "$KIT/ulfo.dmg")
  if grep -q "line 2999" "$WK/m-after/text.txt" && test "$(crashes)" = "$c0"; then verdict "helper survives: no crash reports, a new attach reads"
  else verdict "FAIL: after the stress: crash reports $c0 -> $(crashes), attach '$d'"; fi
  detach_all
}
uninstall() {
  S=uninstall
  if pkgutil --pkgs 2> /dev/null | grep -q '^dev\.mavergreen\.dmg-codecs'; then
    wd 300 sudo -n mavergreen uninstall dmg-codecs >> "$OUT/$S.log" 2>&1; verdict "mavergreen uninstall dmg-codecs: exit $?"
  else sudo -n rm -rf "$P"; verdict "no package receipt: the bare plug-in removed by hand"; fi
  left=$( { test -e "$P" && echo "$P"; test -e /usr/local/mavergreen/dmg-codecs && echo /usr/local/mavergreen/dmg-codecs; pkgutil --pkgs | grep '^dev\.mavergreen\.dmg-codecs'; } 2> /dev/null | tr '\n' ' ')
  test -z "$left" && verdict "nothing of the product remains" || verdict "FAIL: left behind: $left"
  out=$(wd 120 hdiutil attach -nobrowse -readonly -mountpoint "$WK/m-un" "$KIT/ulfo.dmg" 2>&1)
  case $out in *orrupt*) verdict "ULFO is back to: $out" ;; *) verdict "FAIL: ULFO after uninstall: $out"; detach_all ;; esac
}
cleanup() {
  S=cleanup; detach_all; tcc_revoke
  test ! -d "$WK/aside.plugin" || sudo -n mv "$WK/aside.plugin" "$P"
  rm -rf "$WK"; verdict "detached, Accessibility grant taken away, work folder gone (the product stays installed unless step 6 ran)"
}
report() {
  S=report; syslog -k Message S DMGCodecs > "$OUT/plugin-syslog.log" 2> /dev/null
  (cd /var/tmp && tar -czf dc-kit-out.tar.gz dc-kit-out) && say "report: /var/tmp/dc-kit-out.tar.gz (fetch it with stage.sh fetch)"
}

test $# -gt 0 || { sed -n '2,10p' "$0"; exit 2; }
for s in "$@"; do case $s in 1|2|3|4|5|6|8|9) ;; *) echo "unknown step $s"; exit 2 ;; esac; done
for s in "$@"; do
  case $s in 1) preflight ;; 2) install ;; 3) doubleclick ;; 4) apfs ;; 5) stress ;; 6) uninstall ;; 8) cleanup ;; 9) report ;; esac
done
