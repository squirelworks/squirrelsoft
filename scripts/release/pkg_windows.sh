#!/usr/bin/env bash
# Package a staged Windows tree as zip + MSI (MSI when msilib is available).
# Usage: pkg_windows.sh <stage-dir> <display-name> <binary-name> <version> <outdir>
set -euo pipefail
stage="${1:?}"; name="${2:?}"; binary="${3:?}"; version="${4:?}"; out="${5:?}"
out="$(cd "$out" && pwd)"
stage="$(cd "$stage" && pwd)"
stem="$name-$version"
platform=windows-x86_64
parent="$(dirname "$stage")"
base="$(basename "$stage")"

zip_out="$out/$stem-$platform.zip"
(cd "$parent" && zip -qr "$zip_out" "$base")
echo "$zip_out"

# MSI: needs Windows CPython with msilib (not MSYS2's python). Prefer the
# runner's setup-python / py launcher, then fall back to python3.
script_dir="$(cd "$(dirname "$0")" && pwd)"
msi_out="$out/$stem-$platform.msi"
py=""
if command -v py >/dev/null 2>&1; then
  py="py -3.12"
elif command -v python3 >/dev/null 2>&1; then
  py="python3"
elif command -v python >/dev/null 2>&1; then
  py="python"
fi
# Hostedtoolcache path when setup-python ran but msys2 PATH hid it.
if [ -z "$py" ] || ! $py -c "import msilib" >/dev/null 2>&1; then
  for cand in /c/hostedtoolcache/windows/Python/3.12.*/x64/python.exe \
              "/c/Program Files/Python312/python.exe"; do
    for c in $cand; do
      if [ -x "$c" ] && "$c" -c "import msilib" >/dev/null 2>&1; then
        py="$c"
        break 2
      fi
    done
  done
fi
if [ -n "$py" ] && $py "$script_dir/make_msi.py" "$stage" "$name" "$version" "$msi_out" 2>"$out/.msi.err"; then
  echo "$msi_out"
else
  echo "MSI skipped: $(tr '\n' ' ' < "$out/.msi.err" 2>/dev/null || echo "no msilib python")" >&2
  rm -f "$out/.msi.err"
fi
