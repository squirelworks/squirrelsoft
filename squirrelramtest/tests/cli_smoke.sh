#!/bin/sh
set -e
BUILD=${1:-build}
cd "$(dirname "$0")/.."
"$BUILD/squirrelramtest" show --iface fixtures/memory/interface.ismem | grep -q 'xor=ok'
"$BUILD/squirrelramtest" show --iface fixtures/memory/interface.ismem | grep -q 'LY68L6400SLIT'
"$BUILD/squirrelramtest" show --iface fixtures/memory/interface.ismem --spd-cdc fixtures/memory/cdc-spd-stub.sqspd | grep -q 'spd-cdc: applied'
"$BUILD/squirrelramtest" selftest --iface fixtures/memory/interface.ismem --report "$BUILD/cli-ramtest-report.txt" | grep -q 'PASS'
"$BUILD/squirrelramtest" selftest --iface fixtures/memory/interface.ismem --spd-cdc fixtures/memory/cdc-spd-stub.sqspd --report "$BUILD/cli-ramtest-cdc-report.txt" | grep -q 'PASS'
grep -q 'result: PASS' "$BUILD/cli-ramtest-report.txt"
"$BUILD/squirrelramtest" demo | grep -q 'PASS'
echo "cli_smoke ok"
