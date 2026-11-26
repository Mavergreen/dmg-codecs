# platform: macOS-only, with make-fixtures.sh -- prints "<name> <sectors> <sha256>" for a UDTO image,
#           with the sectors of every type-2 (ignore) run zeroed: Apple leaves junk there, libudif serves zeros
import hashlib, plistlib, struct, sys
dmg, raw, name = sys.argv[1:4]
d = open(dmg, 'rb').read(); k = d[-512:]
xo, xl = struct.unpack('>QQ', k[0xd8:0xe8]); sectors = struct.unpack('>Q', k[0x1ec:0x1f4])[0]
img = bytearray(open(raw, 'rb').read())
assert len(img) == sectors * 512, (len(img), sectors)
for b in plistlib.loads(d[xo:xo + xl])['resource-fork']['blkx']:
    m = b['Data']; first = struct.unpack('>Q', m[8:16])[0]; n = struct.unpack('>I', m[0xc8:0xcc])[0]
    for i in range(n):
        t, _, s, c = struct.unpack('>IIQQ', m[0xcc + 40 * i:0xcc + 40 * i + 24])
        if t == 2:
            a = (first + s) * 512; img[a:a + c * 512] = bytes(c * 512)
print(name, sectors, hashlib.sha256(img).hexdigest())
