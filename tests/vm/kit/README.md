# dmg-codecs VM kit

What only a person's 10.9 session shows: double-click in Finder, APFS inside (the release gate, through linuxfs),
a copy interrupted by an eject, and uninstall. Everything else is in `tests/vm/attach.sh` and `tests/vm/paths.sh`.

1. On the host: `sh tests/vm/kit/stage.sh [path/to/dmg-codecs.pkg]` (without a pkg, step 2 copies the bare plug-in).
2. On the VM, logged in at the console: `sh /var/tmp/dc-kit/run.sh 1 2 3 4 5 6 8 9`.
   Steps print `VERDICT` lines; any `FAIL` is a finding. About 10 minutes.
3. On the host: `sh tests/vm/kit/stage.sh fetch` (results go to `.superpowers/kits/dmg-codecs-results/`).

Step 2 grants Accessibility to osascript and System Events so the kit can read DiskImageMounter's and Finder's
dialogs; step 8 takes the grant away. Step 4 tests the release gate when linuxfs is installed; without it, it
records what stock 10.9 does with a zlib APFS image and checks that the LZFSE and xz ones match.
