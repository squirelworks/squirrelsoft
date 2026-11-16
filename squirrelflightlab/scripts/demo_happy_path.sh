#!/bin/sh
# Happy-path demo: AoA sweep (plot + CSV) and a short flight-history recording.
# Usage: demo_happy_path.sh [BUILD_DIR]
set -eu
B=${1:-build}
P=$B/squirrelflightlab
OUT=$B/demo
mkdir -p "$OUT"

echo "== SquirrelFlightLab happy-path demo =="
"$P" project samples/ism-fighter-v0.sqflt | head -8

echo ""
echo "-- AoA sweep --"
"$P" aoa-sweep --project samples/ism-fighter-v0.sqflt \
  --V 22 --alpha0 -4 --alpha1 12 --n 17 \
  --csv "$OUT/aoa_sweep.csv"

echo ""
echo "-- Flight history recording --"
"$P" record --project samples/ism-fighter-v0.sqflt \
  --duration 2 --dt 0.02 --throttle 0.55 --de -2 \
  --csv "$OUT/flight_history.csv"

echo ""
echo "-- Scenarios --"
"$P" scenario trim_cruise --project samples/ism-fighter-v0.sqflt
"$P" scenario pull_up --project samples/ism-fighter-v0.sqflt
"$P" scenario roll --project samples/ism-fighter-v0.sqflt

echo ""
echo "-- Avionics co-check --"
"$P" avionics-check samples/ism-fighter-v0.sqflt | head -5

echo ""
echo "-- ISM params + FCS co-check --"
"$P" ism-load --params fixtures/ism-fighter/ism-fj-1.ismfj-params \
  --iface fixtures/ism-fighter/bay-io.ismfj-iface | head -3
"$P" fcs-check --report "$OUT/fcs_report.txt"

echo ""
echo "Wrote:"
echo "  $OUT/aoa_sweep.csv"
echo "  $OUT/flight_history.csv"
echo "  $OUT/fcs_report.txt"
echo "demo_happy_path ok"
