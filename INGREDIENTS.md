# Build ingredients

Everything baked into the shipped plug-in, and how a change to it reaches a release. This repo is its
own upstream: the plug-in and libudif are Mavergreen code, versioned `YYYYMMDD.N`.

| Ingredient | Pinned in | On a bump |
|---|---|---|
| clang22, the compiler and linker | `components/clang22/version`, `components/clang22/linux-sha256` | a repackage |
| lzfse, Apple's LZFSE decoder (BSD-3-Clause), compiled in | `components/lzfse/{repo,version,sha256}` (a commit and its GitHub tarball's SHA-256) | a repackage |
| xz-embedded, the xz/LZMA2 decoder (0BSD), compiled in | `components/xz-embedded/{repo,version,sha256}` | a repackage |
| zlib and bzip2, for the Linux host tests only (the plug-in links 10.9's own) | `components/zlib/…`, `components/bzip2/…` | tests only |
| MacOSX10.9 SDK, stevedore, packaging helpers | `Mavergreen/shipyard@v1` | `@v1` moves without a pin change |

`build/codecs.sh fetch DIR` fetches the four sources at their pins through shipyard's
`fetch_pinned_source.sh`, which refuses a tarball whose SHA-256 is not the pinned one.
