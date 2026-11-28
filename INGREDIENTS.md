# Build ingredients

Everything baked into the shipped plug-in, and how a change to it reaches a release. This repo is its
own upstream: the plug-in and libudif are Mavergreen code, versioned `YYYYMMDD.N`.

| Ingredient | Pinned in | On a bump |
|---|---|---|
| clang22, the compiler and linker | `components/clang22/version`, `components/clang22/linux-sha256` | ✅ Renovate (`github-releases`); the PR says how to move `linux-sha256`. A new release |
| lzfse, Apple's LZFSE decoder (BSD-3-Clause), compiled in | `components/lzfse/{repo,version,sha256}` (a commit and its GitHub tarball's SHA-256) | ✅ Renovate (`git-refs` on master); the PR says how to move `sha256`. A new release |
| xz-embedded, the xz/LZMA2 decoder (0BSD), compiled in | `components/xz-embedded/{repo,version,sha256}` | ✅ Renovate (`git-refs` on master), as lzfse. A new release |
| zlib and bzip2, for the Linux host tests only (the plug-in links 10.9's own) | `components/zlib/…`, `components/bzip2/…` | ❌ untrackable as yet: test-only sources, pinned by commit and tarball hash; a bump changes no shipped byte |
| MacOSX10.9 SDK, stevedore, packaging helpers | `Mavergreen/shipyard@v1` | ✅ Renovate's github-actions manager; `@v1` moves without a pin change |

`build/codecs.sh fetch DIR` fetches the four sources at their pins through shipyard's
`fetch_pinned_source.sh`, which refuses a tarball whose SHA-256 is not the pinned one.

No upstream release notes: DMG Codecs is its own upstream.

Apple's clang (`/usr/bin/clang`) builds the host unit tests only when a developer asks for sanitizers on a Mac
(`DC_SANITIZE=1 sh tests/c/run.sh`): clang22 ships no ASan or UBSan runtime for any host. Nothing it builds is
shipped, and CI does not use it. It goes away when clang22 carries compiler-rt's sanitizer runtimes.

The Sparkle updater (`CMakeLists.txt`, built by `build/build-updater.sh`) embeds the family's pinned Sparkle
through shipyard, and is signed with the organization's key (`updater/ed25519_key.pub`).

## Declared state

- upstream: UPSTREAM_VERSION
- clang22: components/clang22/version
- lzfse: components/lzfse/version
- xz-embedded: components/xz-embedded/version

## Versioning

- self-upstream-date

## Conformance deviations

- scheme: DMG Codecs is its own upstream (no upstream release to repackage), so it versions itself by date as YYYYMMDD.N with no -mavericks.N axis; exit: when conformance's scheme check reads "## Versioning" (self-upstream-date), drop this
- install-path:Library/Plug-ins/DiskImages/DMGCodecs.plugin*: DiskImages.framework loads CFPlugIns only from Library/Plug-ins/DiskImages; the manifest's outside lists the bundle, so uninstall removes it
- no-standalone:dmg-codecs: the product is a DiskImages plug-in, not a tool to run from a folder: it works only installed root-owned in /Library/Plug-ins/DiskImages, where the pkg's receipt and manifest let uninstall remove it, so a tarball would only be a way to install it untracked
