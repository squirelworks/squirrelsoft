#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelismbench
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q keyboard-test || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P keyboard-test | grep -q PASS || fail "keyboard-test"
$P mouse-test | grep -q PASS || fail "mouse-test"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelismbench cli_smoke ok"
