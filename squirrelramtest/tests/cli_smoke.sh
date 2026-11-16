#!/bin/sh
set -e
BUILD=${1:-build}
cd "$(dirname "$0")/.."
"$BUILD/squirrelramtest" show --iface fixtures/memory/interface.ismem | grep -q 'xor=ok'
"$BUILD/squirrelramtest" show --iface fixtures/memory/interface.ismem | grep -q 'LY68L6400SLIT'
"$BUILD/squirrelramtest" selftest --iface fixtures/memory/interface.ismem --report "$BUILD/cli-ramtest-report.txt" | grep -q 'PASS'
grep -q 'result: PASS' "$BUILD/cli-ramtest-report.txt"
"$BUILD/squirrelramtest" demo | grep -q 'PASS'
echo "cli_smoke ok"
