#!/usr/bin/env bash
# Print release metadata for one app as KEY=VALUE lines.
# Usage: scripts/release/app_meta.sh <app-folder>
#
# Android apps take versionName/versionCode from build.gradle.kts.
# Desktop-only C programs take the version from <app>/VERSION.
# The special name "desktop" is a suite release of every C desktop program.
set -euo pipefail
app="${1:?usage: app_meta.sh <app-folder>}"
root="$(cd "$(dirname "$0")/../.." && pwd)"

case "$app" in
  desktop)
    name=SquirrelSoft-Desktop
    desktop=suite
    version=$(date -u +%Y.%m.%d)
    code=0
    tag="desktop-v$version"
    notes="docs/releases/desktop-$version.md"
    echo "app=$app"
    echo "name=$name"
    echo "desktop=$desktop"
    echo "version=$version"
    echo "version_code=$code"
    echo "tag=$tag"
    echo "notes=$notes"
    echo "binary="
    exit 0
    ;;
  squirrelsound)          name=SquirrelSound;         desktop=c;      binary=squirrelsound ;;
  squirrelsuica)          name=SquirrelSuica;         desktop=c;      binary=squirrelsuica ;;
  squirrelsuica-analysis) name=SquirrelSuicaAnalysis; desktop=python; binary= ;;
  squirrelqr)             name=SquirrelQR;            desktop=c;      binary=squirrelqr ;;
  squirrelstation)        name=SquirrelStation;       desktop=c;      binary=squirrelstation ;;
  squirrelcad)            name=SquirrelCAD;           desktop=c;      binary=squirrelcad ;;
  squirrelgnss)           name=SquirrelGNSS;          desktop=c;      binary=squirrelgnss ;;
  squirrelcalc)           name=SquirrelCalc;          desktop=c;      binary=squirrelcalc ;;
  squirrelpaint)          name=SquirrelPaint;         desktop=c;      binary=squirrelpaint ;;
  squirrelgames)          name=SquirrelGames;         desktop=c;      binary=squirrelgames ;;
  squirreldebug)          name=SquirrelDebug;         desktop=c;      binary=squirreldebug ;;
  *) echo "unknown app: $app" >&2; exit 2 ;;
esac

gradle="$root/$app/android/app/build.gradle.kts"
verfile="$root/$app/VERSION"
if [ -f "$gradle" ]; then
  version=$(sed -n 's/^ *versionName *= *"\([^"]*\)".*/\1/p' "$gradle" | head -1)
  code=$(sed -n 's/^ *versionCode *= *\([0-9][0-9]*\).*/\1/p' "$gradle" | head -1)
  [ -n "$version" ] && [ -n "$code" ] || { echo "no versionName/versionCode in $gradle" >&2; exit 2; }
elif [ -f "$verfile" ]; then
  version=$(tr -d '[:space:]' < "$verfile")
  code=0
  [ -n "$version" ] || { echo "empty VERSION in $verfile" >&2; exit 2; }
else
  echo "no android/build.gradle.kts or VERSION for $app" >&2
  exit 2
fi

echo "app=$app"
echo "name=$name"
echo "desktop=$desktop"
echo "binary=$binary"
echo "version=$version"
echo "version_code=$code"
echo "tag=$app-v$version"
echo "notes=docs/releases/$app-$version.md"
