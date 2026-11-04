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
echo "squirrelqr cli smoke ok"
