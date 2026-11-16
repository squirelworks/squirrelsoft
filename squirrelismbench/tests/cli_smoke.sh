#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelismbench
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q keyboard-test || fail "help"
$P help | grep -q memory-test || fail "help memory"
$P help | grep -q typewriter-test || fail "help typewriter"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P keyboard-test | grep -q PASS || fail "keyboard-test"
$P typewriter-test | grep -q PASS || fail "typewriter-test"
$P mouse-test | grep -q PASS || fail "mouse-test"
$P memory-test | grep -q PASS || fail "memory-test"
$P memory-test | grep -q "NR Ch.7.1" || fail "memory NR cite"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelismbench cli_smoke ok"
