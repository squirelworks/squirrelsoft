#!/usr/bin/env python3
"""SquirrelQR desktop: PNG reader tests.

Writes QR symbols from libs/sqqr/fixtures/symbols.txt as PNG files in every
colour type and bit depth, with each row filter, interlaced or not, with
transparency, and with several zlib settings (stored, fixed and dynamic
Huffman blocks). Each PNG must load to exactly the gray image computed here
(checked with test_desktop --same against a PGM), and some are decoded end to
end with the squirrelqr CLI. Damaged files must fail cleanly.

Usage: python3 -I desktop/tests/test_png.py BUILD   (from squirrelqr/)
"""
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIXTURES = HERE.parent.parent.parent / "libs" / "sqqr" / "fixtures" / "symbols.txt"
BUILD = Path(sys.argv[1])
failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg, file=sys.stderr)


def symbols():
    lines = FIXTURES.read_text().splitlines()
    out, i = [], 0
    while i < len(lines):
        f = lines[i].split()
        if f and f[0] == "S":
            n = 17 + 4 * int(f[1])
            grid = [[(int(lines[i + 1 + r][c // 4], 16) >> (3 - c % 4)) & 1 for c in range(n)] for r in range(n)]
            out.append((grid, bytes.fromhex(f[4])))
            i += n
        i += 1
    return out


# ------------------------------------------------------------ PNG writer ----

def chunk(typ, data):
    return struct.pack(">I", len(data)) + typ + data + struct.pack(">I", zlib.crc32(typ + data))


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def filt(rows, bpp, mode):
    """Filter raw rows; mode is 0-4 or 'cycle' (row y uses filter y % 5)."""
    out, prev = b"", bytes(len(rows[0])) if rows else b""
    for y, row in enumerate(rows):
        f = y % 5 if mode == "cycle" else mode
        res = bytearray()
        for i, x in enumerate(row):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            pred = (0, a, b, (a + b) >> 1, paeth(a, b, c))[f]
            res.append((x - pred) & 0xFF)
        out += bytes([f]) + res
        prev = row
    return out


def pack(samples, depth):
    """One row of samples at the bit depth, MSB first, padded to a byte."""
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out, acc, nb = bytearray(), 0, 0
    for s in samples:
        acc = acc << depth | s
        nb += depth
        if nb == 8:
            out.append(acc)
            acc = nb = 0
    if nb:
        out.append(acc << (8 - nb))
    return bytes(out)


ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


def png(pixels, w, h, ctype, depth, *, plte=None, trns=None, interlace=0, mode="cycle",
        level=6, strategy=zlib.Z_DEFAULT_STRATEGY, idat_split=0, extra=b""):
    """pixels[y][x] is a tuple of channel samples."""
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, ch * depth // 8)
    passes = ADAM7 if interlace else [(0, 0, 1, 1)]
    raw = b""
    for x0, y0, dx, dy in passes:
        xs, ys = range(x0, w, dx), range(y0, h, dy)
        if not xs or not ys:
            continue
        rows = [pack([s for x in xs for s in pixels[y][x]], depth) for y in ys]
        raw += filt(rows, bpp, mode)
    co = zlib.compressobj(level, zlib.DEFLATED, 15, 9, strategy)
    z = co.compress(raw) + co.flush()
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    out += chunk(b"tEXt", b"Comment\x00made-up test image") + extra
    if plte:
        out += chunk(b"PLTE", b"".join(bytes(e) for e in plte))
    if trns is not None:
        out += chunk(b"tRNS", trns)
    parts = [z] if not idat_split else [z[i:i + idat_split] for i in range(0, len(z), idat_split)]
    for p in parts:
        out += chunk(b"IDAT", p)
    return out + chunk(b"IEND", b"")


# ------------------------------------------------- expected gray values ----

def luma(r, g, b):
    return (77 * r + 150 * g + 29 * b + 128) >> 8


def over_white(v, a):
    return (v * a + 255 * (255 - a) + 127) // 255


def to8(v, depth):
    return v >> 8 if depth == 16 else v * 255 // ((1 << depth) - 1)


def pgm(gray, w, h):
    return f"P5\n{w} {h}\n255\n".encode() + bytes(gray)


# ------------------------------------------------------------- variants ----

def variants(grid, scale):
    """(name, png bytes, expected gray) for one symbol."""
    n = len(grid)
    side = (n + 8) * scale

    def dark(x, y):
        r, c = y // scale - 4, x // scale - 4
        return 0 <= r < n and 0 <= c < n and grid[r][c]

    def build(sample, expect):
        px = [[sample(x, y, dark(x, y)) for x in range(side)] for y in range(side)]
        ex = [expect(px[y][x]) for y in range(side) for x in range(side)]
        return px, ex

    out = []

    def add(name, ctype, depth, sample, expect, **kw):
        px, ex = build(sample, expect)
        out.append((name, png(px, side, side, ctype, depth, **kw), ex))

    for depth in (1, 2, 4, 8, 16):
        top = (1 << depth) - 1
        lo = 0 if depth < 4 else top // 12
        add(f"gray{depth}", 0, depth, lambda x, y, d, lo=lo, top=top, depth=depth: (lo if d else top - (x * y) % 2 * (depth >= 4),),
            lambda p, depth=depth: to8(p[0], depth))
    add("gray8-interlaced", 0, 8, lambda x, y, d: (20 if d else 235,), lambda p: p[0], interlace=1)
    add("gray1-interlaced", 0, 1, lambda x, y, d: (0 if d else 1,), lambda p: p[0] * 255, interlace=1)
    for f in range(5):
        add(f"gray8-filter{f}", 0, 8, lambda x, y, d: ((x + y) % 7 if d else 240 - (x * 3 + y) % 9,),
            lambda p: p[0], mode=f)
    for depth in (8, 16):
        s = 257 if depth == 16 else 1
        add(f"rgb{depth}", 2, depth,
            lambda x, y, d, s=s: ((10 * s, 40 * s + x % 3, 120 * s) if d else (250 * s, 240 * s, 200 * s + y % 5)),
            lambda p, depth=depth: luma(*(to8(v, depth) for v in p)))
        add(f"gray-alpha{depth}", 4, depth,
            lambda x, y, d, s=s: ((15 * s, 255 * s) if d else (0, (x * 7 + y) % 3 * 40 * s)),
            lambda p, depth=depth: over_white(to8(p[0], depth), to8(p[1], depth)))
        add(f"rgba{depth}-transparent", 6, depth,
            lambda x, y, d, s=s: ((30 * s, 20 * s, 60 * s, 255 * s) if d else (0, 0, 0, (x + y) % 2 * 30 * s)),
            lambda p, depth=depth: over_white(luma(*(to8(v, depth) for v in p[:3])), to8(p[3], depth)))
    add("rgba8-interlaced", 6, 8, lambda x, y, d: (0, 0, 0, 255) if d else (255, 255, 255, 0),
        lambda p: over_white(luma(*p[:3]), p[3]), interlace=1)
    pal = [(250, 250, 250), (5, 5, 30), (250, 230, 160), (190, 220, 250)] + [(200 + i % 56,) * 3 for i in range(4, 256)]
    for depth in (1, 2, 4, 8):
        size = 1 << depth
        entries = pal[:size]
        add(f"palette{depth}", 3, depth, lambda x, y, d, size=size: (1 if d else ((x + y) % size if (x + y) % size != 1 else 0),),
            lambda p, entries=entries: luma(*entries[p[0]]), plte=entries)
    alphas = bytes([0, 255, 128])
    add("palette4-trns", 3, 4, lambda x, y, d: (1 if d else (0 if (x + y) % 3 else 2),),
        lambda p: over_white(luma(*pal[p[0]]), alphas[p[0]] if p[0] < 3 else 255),
        plte=pal[:16], trns=alphas)
    add("gray16-trns", 0, 16, lambda x, y, d: (1000 if d else (65000 if (x + y) % 2 else 777),),
        lambda p: 255 if p[0] == 777 else p[0] >> 8, trns=struct.pack(">H", 777))
    add("rgb8-trns", 2, 8, lambda x, y, d: (0, 0, 0) if d else ((1, 2, 3) if x % 2 else (240, 240, 240)),
        lambda p: 255 if p == (1, 2, 3) else luma(*p), trns=struct.pack(">HHH", 1, 2, 3))
    for name, kw in (("stored", {"level": 0}), ("fast", {"level": 1}), ("best", {"level": 9}),
                     ("fixed", {"strategy": zlib.Z_FIXED}), ("huffman", {"strategy": zlib.Z_HUFFMAN_ONLY}),
                     ("rle", {"strategy": zlib.Z_RLE}), ("split-idat", {"idat_split": 7}),
                     ("ancillary", {"extra": chunk(b"zzZz", b"ignore me")})):
        add(f"gray8-{name}", 0, 8, lambda x, y, d: (25 if d else 230,), lambda p: p[0], **kw)
    return out


def main():
    syms = symbols()
    test_desktop, cli = BUILD / "test_desktop", BUILD / "squirrelqr"
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        count = 0
        for k, (grid, want) in enumerate(syms[:6]):
            for name, data, expect in variants(grid, 3 + k % 2):
                side = int(len(expect) ** 0.5)
                pp, gp = td / f"{k}-{name}.png", td / f"{k}-{name}.pgm"
                pp.write_bytes(data)
                gp.write_bytes(pgm(expect, side, side))
                p = subprocess.run([test_desktop, "--same", pp, gp], capture_output=True)
                check(p.returncode == 0, f"{k}-{name}: {p.stderr.decode().strip()}")
                if k < 2 or name in ("rgba8-transparent", "palette1", "gray8-interlaced"):
                    p = subprocess.run([cli, "--raw", pp], capture_output=True)
                    check(p.returncode == 0 and p.stdout == want + b"\n", f"{k}-{name}: decode {p.returncode} {p.stderr!r}")
                count += 1
        # Damaged or unsupported files fail cleanly with exit 2.
        grid, _ = syms[0]
        good = dict((v[0], v[1]) for v in variants(grid, 3))["gray8"]
        ihdr_end = 8 + 25
        w = struct.unpack(">I", good[16:20])[0]
        rows = b"\x07" + bytes(w) + (b"\x00" + bytes(w)) * (w - 1)
        bad = {
            "truncated": good[:len(good) // 2],
            "no-idat": good[:ihdr_end] + chunk(b"IEND", b""),
            "bad-zlib": good[:ihdr_end] + chunk(b"IDAT", b"\x78\x9c\xff\xff\xff\xff") + chunk(b"IEND", b""),
            "short-data": good[:ihdr_end] + chunk(b"IDAT", zlib.compress(b"\x00" * 10)) + chunk(b"IEND", b""),
            "critical-chunk": good[:ihdr_end] + chunk(b"ZZZZ", b"") + good[ihdr_end:],
            "bad-depth": good[:8] + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 3, 0, 0, 0, 0)) + good[ihdr_end:],
            "bad-filter": good[:ihdr_end] + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""),
            "huge": good[:8] + chunk(b"IHDR", struct.pack(">IIBBBBB", 100000, 100000, 8, 0, 0, 0, 0)) + good[ihdr_end:],
            "palette-without-plte": png([[(0,)]], 1, 1, 3, 8),
        }
        for name, data in bad.items():
            path = td / f"bad-{name}.png"
            path.write_bytes(data)
            p = subprocess.run([cli, path], capture_output=True, timeout=30)
            check(p.returncode == 2 and p.stderr, f"bad-{name}: exit {p.returncode} {p.stderr!r}")
    print(f"squirrelqr PNG: {count} images,", "FAILED" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
