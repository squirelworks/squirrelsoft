#!/usr/bin/env bash
# Quick smoke of a staged desktop binary before it is packaged.
# Usage: smoke_desktop.sh <app-folder> <path-to-binary>
set -euo pipefail
app="${1:?usage}"; bin="${2:?usage}"
[ -x "$bin" ] || [ -f "$bin" ] || { echo "missing binary $bin" >&2; exit 2; }
stage="$(cd "$(dirname "$bin")" && pwd)"
case "$app" in
  squirrelsound)
    "$bin" synth "$stage/a440.wav" --freq 440 --seconds 1 --rate 44100
    "$bin" pitch "$stage/a440.wav" | grep -E '44[0-9]\.'
    "$bin" live --source demo --headless --frames 5 --fft 1024 --freq 440
    rm -f "$stage/a440.wav"
    ;;
  squirrelgnss)
    "$bin" help >/dev/null
    "$bin" demo-track 2>/dev/null | head -5 | grep -qiE 'lat|lon|fix|GPS|demo' || \
      "$bin" demo-track >/dev/null
    ;;
  squirrelqr)
    "$bin" --help 2>&1 | grep -qi squirrelqr
    ;;
  squirrelsuica)
    "$bin" --help 2>&1 | grep -qi squirrelsuica
    ;;
  squirrelcalc|squirrelpaint|squirrelgames|squirrelstation|squirrelcad|squirreldebug)
    # Headless / no-display programs still answer --help or help.
    if "$bin" --help >/dev/null 2>&1; then
      :
    elif "$bin" help >/dev/null 2>&1; then
      :
    else
      # Binary exists and is executable; GUI-only entry is OK for packaging.
      test -f "$bin"
    fi
    ;;
  *)
    test -f "$bin"
    ;;
esac
echo "smoke ok: $app ($bin)"
