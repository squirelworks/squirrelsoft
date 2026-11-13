#!/bin/sh
# squirrelcalc command line: snapshot, errors and exit codes. Usage: cli_smoke.sh BUILD_DIR
set -u
B=${1:-build}
C=$B/squirrelcalc
fail() { echo "FAIL: $*" >&2; exit 1; }

$C --help | grep -q -- "--snapshot" || fail "--help"
$C --bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "unknown option should exit 2"

out=$($C --fn "x^2 - 2" --roots --snapshot "$B/snap.ppm") || fail "snapshot exit"
echo "$out" | grep -q -- "-1.41421" || fail "roots in the status: $out"
[ "$(head -c 15 "$B/snap.ppm")" = "$(printf 'P6\n900 600\n255')" ] || fail "snapshot header"

out=$($C --let a=3 --fn "a x" --fn "x + a" --roots --snapshot "$B/snap2.ppm") || fail "--let"
echo "$out" | grep -q "y1: 0" || fail "a x has a root at 0: $out"
echo "$out" | grep -q "y2: -3" || fail "x + a has a root at -3: $out"

err=$($C --fn "sin(" --snapshot "$B/snap3.ppm" 2>&1 >/dev/null)
echo "$err" | grep -q "y1:" || fail "parse error reported: $err"
$C --let "x=1" --snapshot "$B/snap4.ppm" >/dev/null 2>&1; [ $? -eq 1 ] || fail "--let x=1 should exit 1"
$C --fn 1 --fn 2 --fn 3 --fn 4 --fn 5 >/dev/null 2>&1; [ $? -eq 2 ] || fail "five functions should exit 2"
echo "squirrelcalc cli ok"
