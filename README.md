# DMG Codecs for Mavericks

**This README has not been read or edited by a human yet.** Until it has, this project cannot cut
its first release.

Mac OS X 10.9 Mavericks says this when you open many of today's downloads:

> The following disk images couldn't be opened … corrupt image

    hdiutil: attach failed - corrupt image

The disk image is fine. It is compressed with LZFSE or LZMA (xz), which Apple added after 10.9.
DMG Codecs teaches 10.9 both, so those disk images open the way any other does: double-click,
`hdiutil attach`, `hdiutil convert`. It changes nothing else.

An unofficial community project, not affiliated with Apple.
