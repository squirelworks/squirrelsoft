#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelshipbench
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q hydro-test || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P hydro-test | grep -q PASS || fail "hydro-test"
$P hydro-test | grep -q "NR Ch.4.1" || fail "hydro NR"
$P board-test | grep -q PASS || fail "board-test"
$P heatflux-test | grep -q PASS || fail "heatflux-test"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelshipbench cli_smoke ok"
