#!/usr/bin/env bash
# Print release metadata for one app as KEY=VALUE lines (GitHub Actions output format).
# Usage: scripts/release/app_meta.sh <app-folder>
set -euo pipefail
app="${1:?usage: app_meta.sh <app-folder>}"
root="$(cd "$(dirname "$0")/../.." && pwd)"

case "$app" in
  squirrelsound)          name=SquirrelSound;         desktop=c ;;
  squirrelsuica)          name=SquirrelSuica;         desktop=python ;;
  squirrelsuica-analysis) name=SquirrelSuicaAnalysis; desktop=python ;;
  squirrelqr)             name=SquirrelQR;            desktop=none ;;
  *) echo "unknown app: $app" >&2; exit 2 ;;
esac

gradle="$root/$app/android/app/build.gradle.kts"
version=$(sed -n 's/^ *versionName *= *"\([^"]*\)".*/\1/p' "$gradle" | head -1)
code=$(sed -n 's/^ *versionCode *= *\([0-9][0-9]*\).*/\1/p' "$gradle" | head -1)
[ -n "$version" ] && [ -n "$code" ] || { echo "no versionName/versionCode in $gradle" >&2; exit 2; }

echo "app=$app"
echo "name=$name"
echo "desktop=$desktop"
echo "version=$version"
echo "version_code=$code"
echo "tag=$app-v$version"
echo "notes=docs/releases/$app-$version.md"
