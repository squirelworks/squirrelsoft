#!/bin/sh
set -e
BUILD=${1:-build}
cd "$(dirname "$0")/.."
"$BUILD/squirrelkbdcfg" show --iface fixtures/keyboard-60/interface.ismhid | grep -q ESC
"$BUILD/squirrelkbdcfg" demo | grep -q 'round-trip ok'
echo "cli_smoke ok"
