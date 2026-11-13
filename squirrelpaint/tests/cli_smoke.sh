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

# errors
err=$($P --new 10x10 --do "line 1 2 3" 2>&1); [ $? -eq 1 ] || fail "a bad command should exit 1"
echo "$err" | grep -q "line 1 2 3: line: X0 Y0 X1 Y1" || fail "bad command message: $err"
printf 'size 3\nwiggle 1 2\n' > "$B/bad.txt"
err=$($P --new 10x10 --script "$B/bad.txt" 2>&1); [ $? -eq 1 ] || fail "a bad script should exit 1"
echo "$err" | grep -q "bad.txt:2: wiggle 1 2: unknown" || fail "bad script line: $err"
printf 'hello' > "$B/not.bmp"
err=$($P "$B/not.bmp" --snapshot "$B/snap2.ppm" 2>&1); [ $? -eq 1 ] || fail "a bad file should exit 1"
echo "$err" | grep -q "not a picture this reads" || fail "bad file message: $err"
$P "$B/no-such.bmp" --snapshot "$B/snap2.ppm" >/dev/null 2>&1; [ $? -eq 1 ] || fail "a missing file should exit 1"
$P --new 10x10 --save "$B/no-such-dir/x.bmp" >/dev/null 2>&1; [ $? -eq 1 ] || fail "an unwritable file should exit 1"
echo "squirrelpaint cli ok"
