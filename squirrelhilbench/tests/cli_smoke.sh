#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelhilbench
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q load || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P load | grep -q "connected=1" || fail "load"
$P health | grep -q PASS || fail "health"
mkdir -p "$B"
$P stimulus --csv "$B/hil-stim-replay.csv" | grep -q PASS || fail "stimulus"
[ -f "$B/hil-stim-replay.csv" ] || fail "replay csv missing"
head -1 "$B/hil-stim-replay.csv" | grep -q "t_s,V_mps,alpha_deg" || fail "replay header"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelhilbench cli_smoke ok"
