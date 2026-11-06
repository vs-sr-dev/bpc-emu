"""Tiles PNGs (RGB8, as bpc-cli writes them) into one sheet.

    python tools/sheet.py OUT.png A.png [B.png ...] [--cols N] [--scale S]
"""
import struct
import sys
import zlib


def read_png(path):
    d = open(path, 'rb').read()
    i, idat, w, h = 8, b'', 0, 0
    while i < len(d):
        n, t = struct.unpack('>I4s', d[i:i + 8])
        if t == b'IHDR':
            w, h = struct.unpack('>II', d[i + 8:i + 16])
        elif t == b'IDAT':
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(w * 3)
    for y in range(h):
        f, line = raw[y * (w * 3 + 1)], bytearray(raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)])
        for x in range(len(line)):
            a = line[x - 3] if x >= 3 else 0
            b = prev[x]
            c = prev[x - 3] if x >= 3 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, rows


def main():
    a = sys.argv[1:]
    cols = int(a[a.index('--cols') + 1]) if '--cols' in a else 4
    sc = int(a[a.index('--scale') + 1]) if '--scale' in a else 1
    files = [x for i, x in enumerate(a[1:], 1) if not x.startswith('--') and a[i - 1] not in ('--cols', '--scale')]
    imgs = [read_png(f) for f in files]
    w, h = imgs[0][0], imgs[0][1]
    gw, gh = w * sc + 4, h * sc + 4
    rn = (len(imgs) + cols - 1) // cols
    W, H = gw * cols, gh * rn
    out = [bytearray(b'\x40' * W * 3) for _ in range(H)]
    for k, (iw, ih, rows) in enumerate(imgs):
        ox, oy = (k % cols) * gw + 2, (k // cols) * gh + 2
        for y in range(ih * sc):
            src = rows[y // sc]
            for x in range(iw * sc):
                p = (x // sc) * 3
                out[oy + y][(ox + x) * 3:(ox + x) * 3 + 3] = src[p:p + 3]
    raw = b''.join(b'\0' + bytes(r) for r in out)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + \
        chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(a[0], 'wb').write(png)


if __name__ == '__main__':
    main()
