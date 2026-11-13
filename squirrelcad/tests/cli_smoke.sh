#!/bin/sh
# squirrelcad run and plot: scripts, errors and exit codes. Usage: cli_smoke.sh BUILD_DIR
set -u
B=${1:-build}
C=$B/squirrelcad
fail() { echo "FAIL: $*" >&2; exit 1; }
D=$B/smoke.sqdwg
rm -f "$D"
cat > "$B/smoke.scr" <<'SCR'
# a lot with a peg
TITLE Lot_7
LAYER NEW boundary 1
PLINE 0,0 30,0 30,20 0,20 CLOSE
AREA LAST
LAYER SET 0
LINE 0,0 0.5,0.5
BLOCK peg 0,0 LAST
INSERT peg 30,20
TEXT 10,10 Lot 7
PLOT smoke.svg A4 1:200
SCR
out=$(cd "$B" && ./squirrelcad run smoke.sqdwg smoke.scr) || fail "run: $out"
echo "$out" | grep -q "^area 600  perimeter 100$" || fail "area: $out"
echo "$out" | grep -q "^saved smoke.sqdwg$" || fail "saved: $out"
grep -q "Scale 1:200  Sheet A4" "$B/smoke.svg" || fail "plot"
out=$(echo "LIST" | $C run "$D") || fail "list"
echo "$out" | grep -q "Lot_7: 4 entities" || fail "list: $out"
echo "$out" | grep -q "saved" && fail "LIST should not save"
printf 'MOVE ALL 0,0 @10,0\nEXTENTS\n' | $C run "$D" | grep -q "^10,0 to 40.5,20.5$" || fail "move+extents"
printf 'LINE 0,0\n' | $C run "$D" >/dev/null 2>&1; [ $? -eq 1 ] || fail "a bad line should exit 1"
printf 'LINE 0,0\n' | $C run "$D" 2>&1 | grep -q "^stdin:1: LINE P1 P2" || fail "error line number"
$C plot "$D" "$B/smoke2.svg" A3 PORTRAIT | grep -q "on A3" || fail "plot command"
$C plot "$D" "$B/smoke2.svg" B9 >/dev/null; [ $? -eq 1 ] || fail "bad sheet should exit 1"
$C run >/dev/null 2>&1; [ $? -eq 2 ] || fail "run without a drawing should exit 2"
$C run "$D" "$B/missing.scr" >/dev/null 2>&1; [ $? -eq 1 ] || fail "missing script should exit 1"
echo "squirrelcad run/plot smoke ok"
