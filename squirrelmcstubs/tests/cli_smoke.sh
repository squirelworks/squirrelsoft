#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelmcstubs
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q catalog || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P catalog | grep -q "mc.stores_config" || fail "catalog topic"
$P catalog | grep -q "catalog PASS" || fail "catalog"
$P tick | grep -q "MC tick 1 PASS" || fail "tick mc"
$P tick | grep -q "SMS tick 1 PASS" || fail "tick sms"
$P tick | grep -q "DISP tick 1 PASS" || fail "tick disp"
$P tick | grep -q "tick ok" || fail "tick ok"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelmcstubs cli_smoke ok"
