#!/bin/sh
# squirrelflightlab CLI smoke. Usage: cli_smoke.sh BUILD_DIR
set -u
B=${1:-build}
P=$B/squirrelflightlab
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q aoa-sweep || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args → 2"
$P bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "bad command → 2"

out=$($P project) || fail "project"
echo "$out" | grep -q "fuselage_length_mm: 420" || fail "default fuselage: $out"
echo "$out" | grep -q "SquirrelFlightLab project 1" || fail "header"

out=$($P aoa-sweep --n 5 --alpha0 0 --alpha1 8 --V 20) || fail "aoa-sweep"
echo "$out" | grep -q "alpha_deg,CL" || fail "aoa csv header"
echo "$out" | grep -q "CL vs alpha" || fail "ascii plot"

$P record --duration 0.2 --dt 0.05 --csv "$B/hist.csv" >/dev/null || fail "record"
head -1 "$B/hist.csv" | grep -q "^t_s,V_mps" || fail "hist header"
[ "$(wc -l < "$B/hist.csv")" -ge 3 ] || fail "hist rows"

$P scenario trim_cruise >/dev/null || fail "trim_cruise"
$P scenario pull_up >/dev/null || fail "pull_up"
$P scenario roll >/dev/null || fail "roll"
$P scenario nope >/dev/null 2>&1; [ $? -eq 2 ] || fail "bad scenario → 2"

$P avionics-check >/dev/null || fail "avionics-check"
$P ism-load --params fixtures/ism-fighter/ism-fj-1.ismfj-params \
  --iface fixtures/ism-fighter/bay-io.ismfj-iface | grep -q PASS || fail "ism-load"
$P fcs-check --report "$B/fcs_report.txt" | grep -q "result: PASS" || fail "fcs-check"
grep -q "result: PASS" "$B/fcs_report.txt" || fail "fcs report file"
$P planned | grep -q cfd_mesh_import || fail "planned"
$P cfd-import x.msh >/dev/null 2>&1; [ $? -eq 1 ] || fail "cfd stub exit 1"
$P tunnel x.csv >/dev/null 2>&1; [ $? -eq 1 ] || fail "tunnel stub exit 1"
$P hil /dev/ttyUSB0 >/dev/null 2>&1; [ $? -eq 1 ] || fail "hil stub exit 1"

# Sample project file
S=samples/ism-fighter-v0.sqflt
if [ -f "$S" ]; then
  $P project "$S" | grep -q "ism-fighter-v0" || fail "load sample"
  $P aoa-sweep --project "$S" --n 3 >/dev/null || fail "aoa with sample"
fi

echo "squirrelflightlab cli_smoke ok"
