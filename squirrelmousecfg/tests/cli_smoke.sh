#!/bin/sh
set -e
BUILD=${1:-build}
cd "$(dirname "$0")/.."
"$BUILD/squirrelmousecfg" show --iface fixtures/mouse/interface.ismhid | grep -q 'dpi profile'
"$BUILD/squirrelmousecfg" show --iface fixtures/mouse/interface.ismhid | grep -q 'side1'
"$BUILD/squirrelmousecfg" dpi-set 1600 --iface fixtures/mouse/interface.ismhid | grep -q 'active_dpi=1600'
"$BUILD/squirrelmousecfg" button-map side1 forward --iface fixtures/mouse/interface.ismhid | grep -q 'side1 -> forward'
"$BUILD/squirrelmousecfg" demo | grep -q 'round-trip ok'
echo "cli_smoke ok"
