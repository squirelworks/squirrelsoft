#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelismbench
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q keyboard-test || fail "help"
$P help | grep -q printer-test || fail "help printer"
$P help | grep -q plotter-test || fail "help plotter"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P keyboard-test | grep -q PASS || fail "keyboard-test"
$P typewriter-test | grep -q PASS || fail "typewriter-test"
$P mouse-test | grep -q PASS || fail "mouse-test"
$P memory-test | grep -q PASS || fail "memory-test"
$P printer-test | grep -q PASS || fail "printer-test"
$P plotter-test | grep -q PASS || fail "plotter-test"
$P plotter-test | grep -q "NR Ch.3.1" || fail "plotter NR"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelismbench cli_smoke ok"
