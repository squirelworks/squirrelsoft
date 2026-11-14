#!/usr/bin/env bash
# Build and package one desktop program (or the Python SuicaAnalysis zip) into <outdir>.
# Usage: scripts/release/package_desktop.sh <app-folder> <outdir> <platform>
#   platform: linux-x86_64 | windows-x86_64 | macos-arm64 | any
#
# C programs produce:
#   Windows:  zip + MSI (MSI when msilib is present)
#   macOS:    .app inside .dmg
#   Linux:    AppImage + .deb
# Python apps still ship as a source zip (platform "any").
set -euo pipefail
app="${1:?usage: package_desktop.sh <app> <outdir> <platform>}"
out="${2:?usage}"
platform="${3:?usage}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
here="$(cd "$(dirname "$0")" && pwd)"
eval "$("$here/app_meta.sh" "$app" | sed 's/^/meta_/')"
stem="$meta_name-$meta_version"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

case "$meta_desktop" in
  none)
    echo "$app has no desktop build"; exit 0 ;;
  suite)
    echo "package_desktop.sh takes one program; use the release workflow matrix for 'desktop'" >&2
    exit 2 ;;
  python)
    [ "$platform" = any ] || { echo "Python apps package once, as platform 'any'" >&2; exit 2; }
    extra=()
    [ "$app" = squirrelsuica-analysis ] && extra=(squirrelsuica/felica_parse.py squirrelsuica/data)
    (cd "$root" && git archive --format=zip --prefix="$stem/" -o "$out/$stem-python.zip" HEAD \
        "$app" ${extra[@]+"${extra[@]}"} ":(exclude)$app/android")
    echo "$out/$stem-python.zip"
    exit 0
    ;;
  c) ;;
  *) echo "unknown desktop kind $meta_desktop" >&2; exit 2 ;;
esac

binary="${meta_binary:?}"
exe=""
case "$platform" in windows-*) exe=.exe ;; esac

# Build the program (and clean only this app's build dir so parallel matrix jobs
# that share libs/ still work when serialized per runner).
make -C "$root/$app" clean >/dev/null 2>&1 || true
make -C "$root/$app" CC="${CC:-gcc}" "build/$binary"

bin_path="$root/$app/build/$binary$exe"
[ -f "$bin_path" ] || { echo "build failed: missing $bin_path" >&2; exit 1; }

stage_root="$(mktemp -d)"
stage="$stage_root/$stem-$platform"
mkdir -p "$stage"
cp "$bin_path" "$stage/$binary$exe"
[ -f "$root/$app/README.md" ] && cp "$root/$app/README.md" "$stage/"
# Suica ships its station table next to the binary when present.
if [ "$app" = squirrelsuica ] && [ -d "$root/$app/data" ]; then
  cp -a "$root/$app/data" "$stage/"
fi

# Windows: ship MinGW runtime / SDL DLLs the binary needs.
if [[ "$platform" == windows-* ]] && command -v ldd >/dev/null; then
  ldd "$stage/$binary$exe" | awk '/mingw64|ucrt64|clang64/ {print $3}' | sort -u | while read -r dll; do
    [ -n "$dll" ] && [ -f "$dll" ] && cp "$dll" "$stage/"
  done
fi

# Smoke the staged binary (cwd = stage so relative data/ works).
(
  cd "$stage"
  "$here/smoke_desktop.sh" "$app" "./$binary$exe"
)

case "$platform" in
  windows-*)
    "$here/pkg_windows.sh" "$stage" "$meta_name" "$binary$exe" "$meta_version" "$out"
    ;;
  macos-*)
    # Stage uses the bare binary name inside the .app.
    mv "$stage/$binary$exe" "$stage/$binary" 2>/dev/null || true
    "$here/pkg_macos.sh" "$stage" "$meta_name" "$binary" "$meta_version" "$out"
    ;;
  linux-*|*)
    "$here/pkg_linux.sh" "$stage" "$meta_name" "$binary" "$meta_version" "$out"
    ;;
esac

rm -rf "$stage_root"
