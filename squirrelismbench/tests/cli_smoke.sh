#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelismbench
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q keyboard-test || fail "help"
$P help | grep -q printer-test || fail "help printer"
$P help | grep -q plotter-test || fail "help plotter"
$P help | grep -q psu-test || fail "help psu"
$P help | grep -q ups-test || fail "help ups"
$P help | grep -q hid-test || fail "help hid"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P keyboard-test | grep -q PASS || fail "keyboard-test"
$P typewriter-test | grep -q PASS || fail "typewriter-test"
$P mouse-test | grep -q PASS || fail "mouse-test"
$P memory-test | grep -q PASS || fail "memory-test"
$P printer-test | grep -q PASS || fail "printer-test"
$P plotter-test | grep -q PASS || fail "plotter-test"
$P plotter-test | grep -q "NR Ch.3.1" || fail "plotter NR"
$P psu-test | grep -q PASS || fail "psu-test"
$P psu-test | grep -q "NR Ch.4.1" || fail "psu NR"
$P ups-test | grep -q PASS || fail "ups-test"
$P ups-test | grep -q "NR Ch.4.1" || fail "ups NR"
$P hid-test mock | grep -q PASS || fail "hid-test mock"
$P hid-test fixtures/hid/mock-hid-session.sqhid | grep -q PASS || fail "hid fixture"
$P hid-test /dev/hidraw0 >/dev/null 2>&1; [ $? -eq 1 ] || fail "hid live refuse"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelismbench cli_smoke ok"
