#!/bin/sh
set -u
B=${1:-build}
P=$B/squirrelhudtool
TMP=$B/cli_smoke_out
fail() { echo "FAIL: $*" >&2; exit 1; }

mkdir -p "$TMP"
$P help | grep -q new-hud || fail "help"
$P >/dev/null 2>&1; [ $? -eq 2 ] || fail "no args"
$P new-hud --out "$TMP/hud.sqhud" | grep -q wrote || fail "new-hud"
$P new-mfd --out "$TMP/mfd.sqhud" | grep -q wrote || fail "new-mfd"
grep -q 'type: hud' "$TMP/hud.sqhud" || fail "hud content"
grep -q 'type: mfd' "$TMP/mfd.sqhud" || fail "mfd content"
$P export --in fixtures/pages/demo-hud.sqhud --format json | grep -q '"format": "sqhud"' || fail "export json"
$P export --in fixtures/pages/demo-mfd.sqhud --format text | grep -q 'SQHUD page' || fail "export text"
$P demo | grep -q "demo ok" || fail "demo"
echo "squirrelhudtool cli_smoke ok"
