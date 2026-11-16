#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelmcstubs
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q mc-tick || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P catalog | grep -q "catalog PASS" || fail "catalog"
$P mc-tick --ticks 2 | grep -q "MC tick 2 PASS" || fail "mc-tick"
$P sms-tick --ticks 2 | grep -q "SMS tick 2 PASS" || fail "sms-tick"
$P display-tick --ticks 1 | grep -q "DISP tick 1 PASS" || fail "display-tick"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelmcstubs cli_smoke ok"
