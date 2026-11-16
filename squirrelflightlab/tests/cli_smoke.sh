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
$P ism-interfaces fixtures/ism-interfaces | grep -q PASS || fail "ism-interfaces"
$P ism-interfaces fixtures/ism-interfaces-bad-version >/dev/null 2>&1
[ $? -eq 1 ] || fail "ism-interfaces bad version → 1"
$P verify | grep -q "provisional, unvalidated" || fail "verify provisional label"
$P verify | grep -q "0 fail" || fail "verify all pass"
$P viability-report | grep -q "provisional, unvalidated" || fail "viability label"
$P viability-report | grep -q "provisional_pass\|overall: pass" || fail "viability overall"
$P viability-report | grep -q "fail=0" || fail "viability fail=0"
$P loadout-check fixtures/loadouts/legal-symmetric.yaml | grep -q "Summary: .* 0 fail" || fail "loadout legal"
$P loadout-check fixtures/loadouts/illegal-heavy-tip.yaml >/dev/null 2>&1; [ $? -eq 1 ] || fail "illegal heavy → 1"
$P loadout-check fixtures/loadouts/illegal-adapter.yaml >/dev/null 2>&1; [ $? -eq 1 ] || fail "illegal adapter → 1"
$P loadout-check fixtures/loadouts/illegal-envelope.yaml >/dev/null 2>&1; [ $? -eq 1 ] || fail "illegal envelope → 1"
$P fcs-check --report "$B/fcs_report.txt" | grep -q "result: PASS" || fail "fcs-check"
grep -q "result: PASS" "$B/fcs_report.txt" || fail "fcs report file"
$P planned | grep -q cfd_mesh_import || fail "planned"
$P planned | grep -q "wind_tunnel_correlate \[active\]" || fail "tunnel active"
$P planned | grep -q "hardware_in_the_loop \[active\]" || fail "hil active"
$P cfd-import x.msh >/dev/null 2>&1; [ $? -eq 1 ] || fail "cfd stub exit 1"
$P tunnel fixtures/tunnel/soft-scaffold-correlate.csv | grep -q PASS || fail "tunnel correlate"
$P tunnel fixtures/tunnel/soft-scaffold-correlate.csv | grep -q "NR Ch.3.1" || fail "tunnel NR"
$P strip-chart --ppm "$B/strip.ppm" | grep -q "NR Ch.3.1" || fail "strip-chart"
[ -f "$B/strip.ppm" ] || fail "strip ppm"
$P help | grep -q "\-\-gui" || fail "help gui"
$P hil mock | grep -q PASS || fail "hil mock"
$P hil mock | grep -q "NR Ch.3.1" || fail "hil NR"
$P hil fixtures/hil/mock-pcb-session.sqhil | grep -q PASS || fail "hil fixture"
$P hil /dev/ttyUSB0 >/dev/null 2>&1; [ $? -eq 1 ] || fail "hil serial refuse exit 1"
$P hil-chart mock --ppm "$B/hil_strip.ppm" | grep -q "NR Ch.3.1" || fail "hil-chart"
[ -f "$B/hil_strip.ppm" ] || fail "hil-chart ppm"
$P hil-chart /dev/ttyUSB0 >/dev/null 2>&1; [ $? -eq 1 ] || fail "hil-chart serial refuse"

# Sample project file
S=samples/ism-fighter-v0.sqflt
if [ -f "$S" ]; then
  $P project "$S" | grep -q "ism-fighter-v0" || fail "load sample"
  $P aoa-sweep --project "$S" --n 3 >/dev/null || fail "aoa with sample"
fi

echo "squirrelflightlab cli_smoke ok"
