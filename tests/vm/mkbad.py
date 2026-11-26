# platform: macOS host, for tests/vm/paths.sh -- writes the bad images the plug-in must decline or fail cleanly on,
#           derived from good fixtures: the Task 2 malformations as files, plus chunks the decoders reject
# usage: python3 -I mkbad.py FIXTURES_DIR OUT_DIR
import os, plistlib, struct, sys
F, O = sys.argv[1:3]

def load(n):
    return bytearray(open(os.path.join(F, n + '.dmg'), 'rb').read())

def put(name, d):
    open(os.path.join(O, 'bad-' + name + '.dmg'), 'wb').write(bytes(d))

def koly(d, off, v):
    d[len(d) - 512 + off:len(d) - 512 + off + 8] = struct.pack('>Q', v)

def runs(d, want):
    k = d[-512:]; dfo = struct.unpack('>Q', k[0x18:0x20])[0]
    xo, xl = struct.unpack('>QQ', k[0xd8:0xe8])
    for b in plistlib.loads(bytes(d[xo:xo + xl]))['resource-fork']['blkx']:
        m = b['Data']; ds = struct.unpack('>Q', m[0x18:0x20])[0]; n = struct.unpack('>I', m[0xc8:0xcc])[0]
        for i in range(n):
            t, _, s, c, off, ln = struct.unpack('>IIQQQQ', m[0xcc + 40 * i:0xcc + 40 * i + 40])
            if t == want:
                yield dfo + ds + off, ln, s, c

u = load('ulfo')
put('truncated', u[:-1])
d = u[:]; koly(d, 0xd8, len(d) + 4096); put('xml-past-eof', d)
d = u[:]; koly(d, 0xe0, 1 << 62); put('xml-length', d)
d = u[:]; i = d.find(b'<data>') + 6
while d[i] in b'\n\t ': i += 1
d[i:i + 2] = b'!!'; put('not-base64', d)
d = u[:]; koly(d, 0x20, 50); put('fork-short', d)
# spec: the image decodes, but its master checksum disagrees; attach and verify both refuse it
d = u[:]; d[len(d) - 512 + 0x168] ^= 1; put('master', d)
# spec: every LZFSE (and xz) chunk's magic broken, so the probe claims the image and every compressed read fails
d = u[:]
for off, ln, _, _ in runs(d, 0x80000007): d[off:off + 4] = b'XXXX'
put('lzfse-chunks', d)
d = load('ulmo')
for off, ln, _, _ in runs(d, 0x80000008): d[off:off + 6] = b'XXXXXX'
put('xz-chunks', d)
# spec: tests/vm/paths-vm.sh -- the huge image's file past 4 GiB broken and its file system intact, so it mounts
#       with -noverify and reading that file is a read error mid-copy; the low blocks and the last MiB stay whole
d = load('ulfo-huge')
total = struct.unpack('>Q', d[-512 + 0x1ec:][:8])[0]
for off, ln, s, c in runs(d, 0x80000007):
    if s >= (4 << 30) // 512 and s + c <= total - 2048: d[off:off + 4] = b'XXXX'
put('huge-data', d)
