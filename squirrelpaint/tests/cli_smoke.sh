#!/bin/sh
# squirrelpaint command line: drawing, files, errors and exit codes. Usage: cli_smoke.sh BUILD_DIR
set -u
B=${1:-build}
P=$B/squirrelpaint
fail() { echo "FAIL: $*" >&2; exit 1; }

$P --help | grep -q -- "--snapshot" || fail "--help"
$P --bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "unknown option should exit 2"
$P --new 10by10 >/dev/null 2>&1; [ $? -eq 2 ] || fail "--new 10by10 should exit 2"
$P --new 5000x10 --save "$B/x.bmp" >/dev/null 2>&1; [ $? -eq 2 ] || fail "too big a picture should exit 2"

# draw, save as BMP and PPM, open each again and save it the other way: the same pixels
cat > "$B/draw.txt" <<'END'
# a test picture
color 2060d0
size 5
line 5 5 60 40
fill on
color ff0000
rect 10 30 30 45
ellipse 40 5 60 20
fill 0 47
END
$P --new 64x48 --script "$B/draw.txt" --do "pencil 63 0" --save "$B/a.bmp" || fail "draw and save"
[ "$(head -c 2 "$B/a.bmp")" = "BM" ] || fail "a.bmp is a BMP"
[ "$(wc -c < "$B/a.bmp")" -eq $((54 + 64 * 3 * 48)) ] || fail "a.bmp size"
$P "$B/a.bmp" --save "$B/b.ppm" || fail "open the BMP, save a PPM"
[ "$(head -c 13 "$B/b.ppm")" = "$(printf 'P6\n64 48\n255\n')" ] || fail "b.ppm header"
$P --open "$B/b.ppm" --save "$B/c.bmp" || fail "open the PPM, save a BMP"
cmp -s "$B/a.bmp" "$B/c.bmp" || fail "BMP -> PPM -> BMP changed the picture"

# undo in a script, and the snapshot of the window
$P "$B/a.bmp" --do "color 00ff00" --do "fill 0 47" --do "undo" --save "$B/d.bmp" || fail "undo"
cmp -s "$B/a.bmp" "$B/d.bmp" || fail "a fill and its undo changed the picture"
out=$($P "$B/a.bmp" --snapshot "$B/snap.ppm") || fail "snapshot"
[ "$out" = "64 x 48" ] || fail "snapshot says the size: $out"
[ "$(head -c 15 "$B/snap.ppm")" = "$(printf 'P6\n960 680\n255')" ] || fail "snapshot header"

# selections and transforms: a quarter turn four times, and a flip twice, change nothing
$P "$B/a.bmp" --do "rotate 90" --do "rotate 90" --do "rotate -90" --do "rotate 270" --do "flip h" --do "flip h" \
    --save "$B/e.bmp" || fail "turns"
cmp -s "$B/a.bmp" "$B/e.bmp" || fail "four turns and two flips changed the picture"
$P "$B/a.bmp" --do "select 0 0 15 11" --do "copy" --do "paste 48 36" --do "drop" --do "select 10 10 41 33" \
    --do "crop" --do "resize 64 48 smooth" --do "canvas 70 50" --save "$B/f.ppm" || fail "select, paste, crop, resize"
[ "$(head -c 13 "$B/f.ppm")" = "$(printf 'P6\n70 50\n255\n')" ] || fail "f.ppm size"
$P --new 8x8 --do "color ff0000" --do "fill 0 0" --do "select 0 0 3 3" --do "move 4 4" --save "$B/g.ppm" || fail "save while floating"
[ "$(tail -c 3 "$B/g.ppm" | od -An -tx1 | tr -d ' \n')" = "ff0000" ] || fail "what floats is saved"
[ "$(head -c 14 "$B/g.ppm" | tail -c 3 | od -An -tx1 | tr -d ' \n')" = "ffffff" ] || fail "the hole it left is saved"
err=$($P --new 10x10 --do "crop" 2>&1); [ $? -eq 1 ] || fail "crop with nothing selected should exit 1"
echo "$err" | grep -q "crop: nothing is selected" || fail "crop message: $err"

# PNG: save one, open it again and save a BMP: the same pixels as the BMP
$P "$B/a.bmp" --save "$B/a.png" || fail "save a PNG"
[ "$(head -c 8 "$B/a.png" | od -An -tx1 | tr -d ' \n')" = "89504e470d0a1a0a" ] || fail "a.png signature"
[ "$(wc -c < "$B/a.png")" -lt "$(wc -c < "$B/a.bmp")" ] || fail "a.png is not smaller than the BMP"
$P "$B/a.png" --save "$B/h.bmp" || fail "open the PNG"
cmp -s "$B/a.bmp" "$B/h.bmp" || fail "BMP -> PNG -> BMP changed the picture"
# and a checker that knows nothing of ours: Python's zlib, when Python is here
if command -v python3 >/dev/null 2>&1; then
    python3 - "$B/a.png" "$B/a.bmp" <<'PY' || fail "a.png does not check out"
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read()
assert d[:8] == b'\x89PNG\r\n\x1a\n'
at, idat, kinds = 8, b'', []
while at < len(d):
    n, = struct.unpack('>I', d[at:at + 4])
    kind, body = d[at + 4:at + 8], d[at + 8:at + 8 + n]
    assert struct.unpack('>I', d[at + 8 + n:at + 12 + n])[0] == zlib.crc32(kind + body), kind
    kinds.append(kind)
    if kind == b'IHDR':
        w, h, depth, ct, _, _, lace = struct.unpack('>IIBBBBB', body)
    if kind == b'IDAT':
        idat += body
    at += 12 + n
assert kinds[0] == b'IHDR' and kinds[-1] == b'IEND' and (depth, ct, lace) == (8, 2, 0)
raw, up, rows = zlib.decompress(idat), bytes(3 * w), []
assert len(raw) == h * (3 * w + 1)
for y in range(h):
    f, x = raw[y * (3 * w + 1)], bytearray(raw[y * (3 * w + 1) + 1:(y + 1) * (3 * w + 1)])
    for i in range(3 * w):
        a, b, c = (x[i - 3] if i >= 3 else 0), up[i], (up[i - 3] if i >= 3 else 0)
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        x[i] = (x[i] + [0, a, b, (a + b) // 2, a if pa <= pb and pa <= pc else b if pb <= pc else c][f]) & 255
    rows.append(bytes(x))
    up = x
bmp = open(sys.argv[2], 'rb').read()
stride = (3 * w + 3) // 4 * 4
for y in range(h):
    r = bmp[54 + (h - 1 - y) * stride:][:3 * w]
    assert rows[y] == bytes(v for i in range(w) for v in r[3 * i:3 * i + 3][::-1]), y
PY
fi

# the tools that take more than one press, by command: the same picture through PNG
printf 'size 4\ntext 2 2 Hi!\nfill on\npolygon 5 20 40 20 20 45\nfill off\ncurve 0 47 63 47 30 0\nspray 50 10\n' > "$B/tools.txt"
$P --new 64x48 --script "$B/tools.txt" --save "$B/t.png" || fail "text, polygon, curve and spray"
$P --new 64x48 --script "$B/tools.txt" --save "$B/t.bmp" || fail "the same as a BMP"
$P "$B/t.png" --save "$B/t2.bmp" || fail "open t.png"
cmp -s "$B/t.bmp" "$B/t2.bmp" || fail "the tools through PNG changed the picture"
cmp -s "$B/t.bmp" "$B/a.bmp" && fail "the tools drew nothing"
err=$($P --new 10x10 --do "polygon 1 1 2 2" 2>&1); [ $? -eq 1 ] || fail "a two-corner polygon should exit 1"
echo "$err" | grep -q "three corners or more" || fail "polygon message: $err"

# errors
err=$($P --new 10x10 --do "line 1 2 3" 2>&1); [ $? -eq 1 ] || fail "a bad command should exit 1"
echo "$err" | grep -q "line 1 2 3: line: X0 Y0 X1 Y1" || fail "bad command message: $err"
printf 'size 3\nwiggle 1 2\n' > "$B/bad.txt"
err=$($P --new 10x10 --script "$B/bad.txt" 2>&1); [ $? -eq 1 ] || fail "a bad script should exit 1"
echo "$err" | grep -q "bad.txt:2: wiggle 1 2: unknown" || fail "bad script line: $err"
printf 'hello' > "$B/not.bmp"
err=$($P "$B/not.bmp" --snapshot "$B/snap2.ppm" 2>&1); [ $? -eq 1 ] || fail "a bad file should exit 1"
echo "$err" | grep -q "not a picture this reads" || fail "bad file message: $err"
head -c 60 "$B/a.png" > "$B/cut.png"
err=$($P "$B/cut.png" --snapshot "$B/snap2.ppm" 2>&1); [ $? -eq 1 ] || fail "a cut PNG should exit 1"
echo "$err" | grep -q "a PNG cut short" || fail "cut PNG message: $err"
$P "$B/no-such.bmp" --snapshot "$B/snap2.ppm" >/dev/null 2>&1; [ $? -eq 1 ] || fail "a missing file should exit 1"
$P --new 10x10 --save "$B/no-such-dir/x.bmp" >/dev/null 2>&1; [ $? -eq 1 ] || fail "an unwritable file should exit 1"
echo "squirrelpaint cli ok"
