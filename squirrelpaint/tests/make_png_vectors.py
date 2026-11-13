#!/usr/bin/env python3
"""Writes tests/png_vectors.h: zlib streams made by Python's zlib and PNG
files made by PIL and by the small writer here, with the pixels each should
give (over white where they are see-through). All of it is made-up data.

    python3 tests/make_png_vectors.py > tests/png_vectors.h
"""
import io, random, struct, zlib
from PIL import Image

A7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
CH = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def chunk(t, b):
    return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def pack_row(samples, depth):
    if depth == 16:
        return b''.join(struct.pack('>H', v) for v in samples)
    if depth == 8:
        return bytes(samples)
    out, acc, nb = bytearray(), 0, 0
    for v in samples:
        acc, nb = acc << depth | v, nb + depth
        if nb == 8:
            out.append(acc)
            acc = nb = 0
    if nb:
        out.append(acc << (8 - nb))
    return bytes(out)


def png(w, h, ct, depth, px, lace, plte=None, trns=None, idat_pieces=1, rng=None):
    """px[y][x] is a tuple of samples; each row gets a filter at random"""
    bpp = max(1, CH[ct] * depth // 8)
    raw = b''
    for x0, y0, dx, dy in A7 if lace else [(0, 0, 1, 1)]:
        xs, ys = range(x0, w, dx), range(y0, h, dy)
        if not xs or not ys:
            continue
        up = None
        for y in ys:
            r = pack_row([s for x in xs for s in px[y][x]], depth)
            f = rng.randrange(5)
            row = bytearray([f])
            for i, v in enumerate(r):
                a = r[i - bpp] if i >= bpp else 0
                b = up[i] if up else 0
                c = up[i - bpp] if up and i >= bpp else 0
                row.append((v - [0, a, b, (a + b) // 2, paeth(a, b, c)][f]) & 255)
            raw += row
            up = r
    z = zlib.compress(raw, 9)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, ct, 0, 0, lace))
    out += chunk(b'tEXt', b'Comment\x00a test picture')
    if plte:
        out += chunk(b'PLTE', bytes(sum(plte, ())))
    if trns is not None:
        out += chunk(b'tRNS', trns)
    step = (len(z) + idat_pieces - 1) // idat_pieces
    for k in range(0, len(z), step):
        out += chunk(b'IDAT', z[k:k + step])
    return out + chunk(b'IEND', b'')


def over_white(r, g, b, a):
    if a == 255:
        return r, g, b
    W = 255 * (255 - a)
    return tuple((c * a + W + 127) // 255 for c in (r, g, b))


def expect(w, h, ct, depth, px, plte=None, trns=None):
    top = (1 << depth) - 1
    sc = lambda v: (v * 255 + top // 2) // top
    key = None
    if trns is not None and ct == 0:
        key = (struct.unpack('>H', trns[:2])[0],) * 3
    if trns is not None and ct == 2:
        key = struct.unpack('>HHH', trns[:6])
    res = []
    for y in range(h):
        for x in range(w):
            s, a = px[y][x], 255
            if ct == 3:
                r, g, b = plte[s[0]]
                if trns is not None and s[0] < len(trns):
                    a = trns[s[0]]
            else:
                rgb = (s[0], s[1], s[2]) if ct in (2, 6) else (s[0],) * 3
                if key and rgb == key:
                    a = 0
                if ct == 4:
                    a = sc(s[1])
                if ct == 6:
                    a = sc(s[3])
                r, g, b = map(sc, rgb)
            res.append(over_white(r, g, b, a))
    return [0xFF000000 | r << 16 | g << 8 | b for r, g, b in res]


def carray(name, data, typ='uint8_t', fmt='0x%02X', per=16):
    lines = ['static const %s %s[%d] = {' % (typ, name, len(data))]
    for i in range(0, len(data), per):
        lines.append('    ' + ', '.join(fmt % v for v in data[i:i + per]) + ',')
    return '\n'.join(lines) + '\n};\n'


def main():
    rng = random.Random(1950)
    print('/* Made by tests/make_png_vectors.py: do not edit. Made-up data only. */')
    print('/* zlib streams from Python\'s zlib of zsrc(i): (i * 7 + i / 13) % 251 for the first 3000,')
    print('   then the 3000 again with every 97th byte turned (a dynamic block); and "abc" x 200 */')
    src = bytes((i * 7 + i // 13) % 251 for i in range(3000))
    src += bytes(v ^ (0x55 if i % 97 == 0 else 0) for i, v in enumerate(src))
    print(carray('Z_DYNAMIC', zlib.compress(src, 9)))
    print(carray('Z_ABC', zlib.compress(b'abc' * 200, 6)))
    print(carray('Z_STORED', zlib.compress(b'stored bytes', 0)))
    cases = []
    # a 2-bit palette picture, interlaced, with see-through entries, its data in three IDATs
    w, h = 13, 9
    plte = [(200, 30, 40), (10, 220, 30), (20, 40, 250), (250, 250, 0)]
    px = [[(rng.randrange(4),) for x in range(w)] for y in range(h)]
    cases.append(('PNG_PAL2', png(w, h, 3, 2, px, 1, plte, b'\x00\x80', 3, rng), w, h,
                  expect(w, h, 3, 2, px, plte, b'\x00\x80')))
    # 16-bit RGB with a transparent colour, interlaced
    w, h = 7, 6
    pool = [(65535, 0, 0), (1234, 40000, 65535), (0, 0, 0)]
    px = [[rng.choice(pool) for x in range(w)] for y in range(h)]
    trns = struct.pack('>HHH', *pool[1])
    cases.append(('PNG_RGB16', png(w, h, 2, 16, px, 1, None, trns, 2, rng), w, h, expect(w, h, 2, 16, px, None, trns)))
    # 4-bit grey, not interlaced
    w, h = 9, 3
    px = [[(rng.randrange(16),) for x in range(w)] for y in range(h)]
    cases.append(('PNG_GRAY4', png(w, h, 0, 4, px, 0, rng=rng), w, h, expect(w, h, 0, 4, px)))
    # PIL's own: grey with alpha, and RGBA
    for name, mode in (('PNG_PIL_LA', 'LA'), ('PNG_PIL_RGBA', 'RGBA')):
        im = Image.new(mode, (6, 5))
        im.putdata([tuple(rng.randrange(256) for _ in mode) for _ in range(30)])
        f = io.BytesIO()
        im.save(f, 'PNG')
        rgba = im.convert('RGBA')
        b = rgba.tobytes()
        want = [over_white(*b[i:i + 4]) for i in range(0, len(b), 4)]
        cases.append((name, f.getvalue(), 6, 5, [0xFF000000 | r << 16 | g << 8 | b for r, g, b in want]))
    for name, data, w, h, want in cases:
        print(carray(name, data))
        print(carray(name + '_PX', want, 'uint32_t', '0x%08X', 6))
    print('static const struct { const uint8_t *png; size_t n; int w, h; const uint32_t *px; } PNG_CASES[] = {')
    for name, data, w, h, want in cases:
        print('    {%s, sizeof %s, %d, %d, %s_PX},' % (name, name, w, h, name))
    print('};')


main()
