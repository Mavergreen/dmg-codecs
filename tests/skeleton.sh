#!/bin/sh
# platform: host-agnostic
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
. "$R/build/msc.sh" 2>/dev/null || { echo "build/msc.sh missing or unsourceable" >&2; exit 1; }
cmp -s "$R/build/msc.sh" "$SHIPYARD_SCRIPTS/templates/msc.sh" || { echo "build/msc.sh differs from shipyard's template" >&2; fail=1; }
v="$(sh "$R/build/version.sh" 2>/dev/null || :)"
echo "$v" | grep -Eq '^[0-9]{8}\.[0-9]+$' || { echo "build/version.sh printed '$v', want YYYYMMDD.N" >&2; fail=1; }
for s in "The following disk images couldn't be opened" "hdiutil: attach failed - corrupt image"; do
  head -n 12 "$R/README.md" 2>/dev/null | grep -qF "$s" || { echo "README.md's opening lacks: $s" >&2; fail=1; }
done
grep -qF "not been read or edited by a human" "$R/README.md" 2>/dev/null || { echo "README.md lacks the not-read-by-a-human line" >&2; fail=1; }
[ "$fail" -eq 0 ] && echo "skeleton OK"
exit "$fail"
