#!/bin/sh
# squirrelgnss command line: outputs and exit codes. Usage: cli_smoke.sh BUILD_DIR
set -u
G=${1:-build}/squirrelgnss
fail() { echo "FAIL: $*" >&2; exit 1; }

[ "$($G llh2ecef 0,0,0)" = "6378137.0000 0.0000 0.0000" ] || fail "llh2ecef"
[ "$($G ecef2llh 0,0,6356752.314245)" = "90.0000000000 0.0000000000 0.0000" ] || fail "ecef2llh at the pole"
$G stakeout 35,139,50 35.001,139,50 | grep -q "N=110.9" || fail "stakeout"
[ "$($G sky | wc -l)" -ge 8 ] || fail "sky"
$G sky 89.9,0,0 | grep -q "^PRN" || fail "sky at the pole"
out=$($G demo-track 3) || fail "demo-track exit"
[ "$(echo "$out" | grep -c 'conv=1')" = 3 ] || fail "demo-track converges: $out"
out=$($G demo-track 2 --fault 3:100 --out "${1:-build}/demo.sqobs") || fail "fault exit"
echo "$out" | grep -q "excluded=PRN03" || fail "fault excluded: $out"
grep -q "excluded=3" "${1:-build}/demo.sqobs" || fail "sqobs excluded"
head -1 "${1:-build}/demo.sqobs" | grep -q "^SQOBS 1$" || fail "sqobs header"
$G demo-track 1 --at -33.86,151.21,20 --noise 0 | grep -q "err E=0.00 N=0.00 U=0.00" || fail "--at noiseless"
B=${1:-build}
$G rinex-demo "$B/demo.26o" "$B/demo.26n" 4 --noise 0 >/dev/null || fail "rinex-demo"
head -1 "$B/demo.26o" | grep -q "OBSERVATION DATA" || fail "rinex-demo obs header"
out=$($G solve "$B/demo.26o" "$B/demo.26n" --truth 35.681236,139.767125,40) || fail "solve exit: $out"
[ "$(echo "$out" | grep -c 'err E=0.00 N=0.00 U=0.00')" = 4 ] || fail "solve noiseless: $out"
echo "$out" | grep -q "^2026-11-13 00:01:30.0 used=" || fail "solve time stamps: $out"
echo "$out" | tail -1 | grep -q "# 4 epochs, 4 fixed" || fail "solve summary: $out"
$G solve "$B/demo.26n" "$B/demo.26n" >/dev/null 2>&1; [ $? -eq 1 ] || fail "solve on a nav file should exit 1"
$G solve "$B/missing.26o" "$B/demo.26n" >/dev/null 2>&1; [ $? -eq 1 ] || fail "solve on a missing file should exit 1"
# RTK: static on both carriers, then a moving rover, from memory and from RINEX
$G rtk-demo 20 --dual --iono 3 | head -1 | grep -q "^fixed epochs=20 .* L1+L2$" || fail "rtk-demo dual"
$G rtk-demo 20 | head -1 | grep -q " L1$" || fail "rtk-demo single"
out=$($G rtk-demo 60 --kinematic --dual --iono 3 --write "$B/kb.26o" "$B/kr.26o" "$B/kn.26n") || fail "rtk-demo kinematic exit"
echo "$out" | tail -1 | grep -q "^# 60 epochs, 5[0-9] fixed" || fail "rtk-demo kinematic: $out"
out=$($G rtk "$B/kb.26o" "$B/kr.26o" "$B/kn.26n" --base-at 35.681236,139.767125,40 --kinematic) || fail "rtk --kinematic exit"
echo "$out" | tail -1 | grep -q "^# 60 epochs, 5[0-9] fixed" || fail "rtk --kinematic: $out"
echo "$out" | grep -q "^00:00:30.0 fixed ratio=.* wl=" || fail "rtk --kinematic lines: $out"
$G rtk-demo --write "$B/x" >/dev/null 2>&1; [ $? -eq 2 ] || fail "rtk-demo --write needs three files"
$G >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args should exit 2"
$G bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "bad command should exit 2"
$G demo-track --fault x >/dev/null 2>&1; [ $? -eq 2 ] || fail "bad --fault should exit 2"
$G help | grep -q demo-track || fail "help"
echo "squirrelgnss cli smoke ok"
