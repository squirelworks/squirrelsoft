#!/bin/sh
# The squirrelstation command line, end to end: every element kind, levels,
# cells, a reference, snaps, measuring, both file formats and DXF export.
# usage: sh tests/cli_smoke.sh BUILD_DIR
set -eu
S="$1/squirrelstation"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
f="$T/a.sqdgn"
fail() {
    echo "cli_smoke: $*" >&2
    exit 1
}
expect() { # expect PATTERN COMMAND...
    pat="$1"
    shift
    out=$("$@" 2>&1) || fail "failed: $* -> $out"
    echo "$out" | grep -q -- "$pat" || fail "$*: wanted '$pat', got: $out"
}

expect "" "$S" new "$f" Smoke
expect "level id=2" "$S" level "$f" walls 0xFF0000
expect "line id=3" "$S" line "$f" 0,0 dl=10,0 --level walls
expect "arc id=4" "$S" arc "$f" 5,5 2 0 360
expect "shape id=5" "$S" shape "$f" 20,0 30,0 30,10 20,10
expect "curve id=6" "$S" curve "$f" 0,20 di=10,0 dl=0,5
expect "solid id=7" "$S" solid "$f" box 40,0,0 2,3,4
expect "solid id=8" "$S" solid "$f" cylinder 50,0,0 1,0,2
expect "mesh id=9" "$S" mesh "$f" 0,30 1,30 1,31
expect "text id=10" "$S" text "$f" 0,40 Lot 12
expect "cell id=" "$S" cell "$f" door 0,0 3
expect "placed cell id=" "$S" place "$f" door 100,0 2 90

expect 'TEXT 10 L1 (0.000,40.000,0.000) "Lot 12"' "$S" list "$f"
expect "cell door (1 elements)" "$S" list "$f"
expect "length=10 " "$S" measure "$f" 3
expect "area=52 volume=24" "$S" measure "$f" 7
expect "area=12.5664" "$S" measure "$f" 4

# the placed door line runs from 100,0 to 100,20 (scale 2, turned 90)
expect "snap=endpoint .*-> 100.000000,20.000000" "$S" snap "$f" 100.1,19.9 0.5
expect "snap=none" "$S" snap "$f" 500,500 0.5

# both formats, either way round
expect "converted" "$S" convert "$f" "$T/a.sqdgnb"
expect "format=sqdgnb" "$S" probe "$T/a.sqdgnb"
expect "format=sqdgn" "$S" probe "$f"
"$S" list "$f" >"$T/l1"
"$S" list "$T/a.sqdgnb" >"$T/l2"
cmp -s "$T/l1" "$T/l2" || fail "text and binary list differently"

# a reference, moved and scaled, is snapped to
expect "" "$S" new "$T/r.sqdgn"
expect "line id=" "$S" line "$T/r.sqdgn" 0,0 1,0
expect "reference 0" "$S" attach "$f" "$T/r.sqdgn" 200,0,0 2
expect "snap=endpoint .* ref=0 .*-> 202.000000,0.000000" "$S" snap "$f" 202.1,0 0.5

# a frozen level is not snapped to
expect "snap=endpoint .*-> 10.000000,0.000000" "$S" snap "$f" 10,0.1 0.2
expect "level walls on frozen" "$S" level "$f" walls freeze
expect "snap=none" "$S" snap "$f" 10,0.1 0.2

expect "exported" "$S" export-dxf "$f" "$T/a.dxf"
grep -q 3DFACE "$T/a.dxf" || fail "no 3DFACE in the DXF"
grep -q VERTEX "$T/a.dxf" || fail "no VERTEX in the DXF"

# bad input is refused, and the file is left alone
cp "$f" "$T/keep"
if "$S" line "$f" 0,0 nonsense >/dev/null 2>&1; then fail "a bad point was taken"; fi
cmp -s "$f" "$T/keep" || fail "a refused command changed the file"
echo "x" >"$T/junk.sqdgn"
if "$S" list "$T/junk.sqdgn" >/dev/null 2>&1; then fail "junk was read"; fi

echo "squirrelstation cli smoke ok"
