#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelbusmon
fail() { echo "FAIL: $*" >&2; exit 1; }

$P help | grep -q decode || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P decode | grep -q "decoded frames=" || fail "decode"
$P decode | grep -q "fcc.surface_cmd" || fail "decode topic"
$P decode | grep -q "OPAQUE topic=mc.stores_config" || fail "opaque stores"
$P filter --node MC | grep -q "filtered frames=" || fail "filter node"
$P filter --msgid 0x0101 | grep -q "fcc.surface_cmd" || fail "filter msgid"
$P filter --topic pdu.power_status | grep -q "pdu.power_status" || fail "filter topic"
$P replay | grep -q "REPLAY v1" || fail "replay header"
$P replay | grep -q "inject t_ms=" || fail "replay inject"
OUT=$B/replay-out.txt
$P replay --out "$OUT" | grep -q "replay wrote" || fail "replay out"
grep -q "REPLAY v1" "$OUT" || fail "replay file"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelbusmon cli_smoke ok"
