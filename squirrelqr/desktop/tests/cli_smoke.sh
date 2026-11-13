#!/bin/sh
# Smoke test the squirrelqr CLI on images written by test_desktop.
set -e
b="${1:?build dir}"
q="$b/squirrelqr"
"$q" --version | grep -q squirrelqr
"$q" --help | grep -q -- --select
out=$("$q" "$b/fmt_P5.img")
[ -n "$out" ]
"$q" --json "$b/fmt_bmp24.img" | grep -q '"corners":\['
"$q" -v "$b/fmt_P6.img" 2>&1 >/dev/null | grep -q version
# Several files at once: one line per file.
[ "$("$q" "$b/fmt_P5.img" "$b/fmt_P2.img" | wc -l)" -eq 2 ]
# Not a QR code -> exit 1; missing file -> exit 2; bad usage -> exit 2.
printf 'P5\n64 64\n255\n' > "$b/blank.pgm"; head -c 4096 /dev/zero | tr '\0' '\377' >> "$b/blank.pgm"
set +e
"$q" "$b/blank.pgm" 2>/dev/null; [ $? -eq 1 ] || { echo "blank: wrong exit"; exit 1; }
"$q" "$b/does-not-exist.pgm" 2>/dev/null; [ $? -eq 2 ] || { echo "missing: wrong exit"; exit 1; }
"$q" 2>/dev/null; [ $? -eq 2 ] || { echo "no args: wrong exit"; exit 1; }
"$q" --json --raw "$b/fmt_P5.img" 2>/dev/null; [ $? -eq 2 ] || { echo "json+raw: wrong exit"; exit 1; }
"$q" --region 1,2,x "$b/fmt_P5.img" 2>/dev/null; [ $? -eq 2 ] || { echo "bad region: wrong exit"; exit 1; }
# Making codes: PNG, SVG, PGM and the terminal, each read back
"$q" --encode "https://squirrel.works/" --out "$b/made.png" 2>/dev/null
[ "$("$q" "$b/made.png")" = "https://squirrel.works/" ] || { echo "made.png: wrong text"; exit 1; }
# the PNG is valid for an independent reader too (zlib + CRCs checked by Python)
python3 -I - "$b/made.png" <<'PY'
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
assert d[:8] == b"\x89PNG\r\n\x1a\n"
p, idat = 8, b""
while p < len(d):
    n, t = struct.unpack(">I4s", d[p:p + 8])
    body = d[p + 8:p + 8 + n]
    assert struct.unpack(">I", d[p + 8 + n:p + 12 + n])[0] == zlib.crc32(t + body), t
    if t == b"IHDR":
        w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
    if t == b"IDAT":
        idat += body
    p += 12 + n
raw = zlib.decompress(idat)
assert depth == 8 and ctype == 0 and len(raw) == h * (w + 1), (w, h, len(raw))
PY
"$q" --encode "SVG TEST" --out "$b/made.svg" 2>/dev/null
grep -q '<path fill="#000" d="M' "$b/made.svg"
"$q" --encode "1234567890" --level H --scale 3 --border 2 --out "$b/made.pgm" 2>/dev/null
[ "$("$q" -v "$b/made.pgm" 2>&1 >/dev/null | grep -c -- '-H,')" -eq 1 ] || { echo "level H not used"; exit 1; }
[ "$("$q" --encode hi 2>/dev/null | wc -l)" -ge 12 ] || { echo "terminal drawing too short"; exit 1; }
printf 'from stdin\n' | "$q" --encode - --out "$b/stdin.png" 2>/dev/null
[ "$("$q" "$b/stdin.png")" = "from stdin" ] || { echo "stdin: wrong text"; exit 1; }
# Wi-Fi: build, read back with details and JSON fields
"$q" --wifi "Nest; 2.4" --password 'a:b' --hidden --out "$b/wifi.png" 2>/dev/null
"$q" --details "$b/wifi.png" | grep -q '  Network: Nest; 2.4' || { echo "wifi details"; exit 1; }
"$q" --details "$b/wifi.png" | grep -q '  Hidden: yes'
"$q" --json "$b/wifi.png" | grep -q '"type":"wifi","fields":\[\["Network","Nest; 2.4"\]'
"$q" --json "$b/made.png" | grep -q '"type":"url"'
set +e
head -c 3000 /dev/zero | tr '\0' 'x' > "$b/huge.txt"
"$q" --encode - --level H < "$b/huge.txt" 2>/dev/null; [ $? -eq 1 ] || { echo "too long: wrong exit"; exit 1; }
"$q" --encode x --level Z 2>/dev/null; [ $? -eq 2 ] || { echo "bad level: wrong exit"; exit 1; }
"$q" --encode x --out "$b/x.gif" 2>/dev/null; [ $? -eq 2 ] || { echo "bad extension: wrong exit"; exit 1; }
"$q" --wifi n --security WPA3 2>/dev/null; [ $? -eq 2 ] || { echo "bad security: wrong exit"; exit 1; }
"$q" --encode x "$b/made.png" 2>/dev/null; [ $? -eq 2 ] || { echo "encode+file: wrong exit"; exit 1; }
set -e
echo "squirrelqr cli smoke ok"
