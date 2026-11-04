#!/usr/bin/env bash
# Build and package one app's desktop counterpart into <outdir>.
# Usage: scripts/release/package_desktop.sh <app-folder> <outdir> <platform>
#   platform: linux-x86_64 | windows-x86_64 | macos-arm64 | any
# C apps are built with the repo Makefiles; Python apps are packaged as source
# (they run on Linux, Windows and macOS with Python 3.9+).
set -euo pipefail
app="${1:?usage}"; out="${2:?usage}"; platform="${3:?usage}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
eval "$("$root/scripts/release/app_meta.sh" "$app" | sed 's/^/meta_/')"
stem="$meta_name-$meta_version"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

case "$meta_desktop" in
  none)
    echo "$app has no desktop build yet"; exit 0 ;;
  python)
    [ "$platform" = any ] || { echo "Python apps package once, as platform 'any'" >&2; exit 2; }
    # Tracked files only, without the Android project.
    (cd "$root" && git archive --format=zip --prefix="$stem/" -o "$out/$stem-python.zip" HEAD \
        "$app" ":(exclude)$app/android")
    echo "$out/$stem-python.zip" ;;
  c)
    make -C "$root/$app" clean >/dev/null 2>&1 || true
    make -C "$root/libs/sqdsp" clean >/dev/null
    make -C "$root/$app" CC="${CC:-gcc}" build/squirrelsound
    stage="$(mktemp -d)/$stem-$platform"
    mkdir -p "$stage"
    exe=""; case "$platform" in windows-*) exe=.exe ;; esac
    cp "$root/$app/build/squirrelsound$exe" "$stage/"
    cp "$root/$app/README.md" "$stage/"
    # Smoke test the packaged binary before shipping it.
    "$stage/squirrelsound$exe" synth "$stage/a440.wav" --freq 440 --seconds 1 --rate 44100
    "$stage/squirrelsound$exe" pitch "$stage/a440.wav" | grep -E '44[0-9]\.'
    "$stage/squirrelsound$exe" live --source demo --headless --frames 5 --fft 1024 --freq 440
    rm -f "$stage/a440.wav"
    case "$platform" in
      windows-*)
        # Ship the MinGW runtime DLLs the binary needs (SDL2 and friends).
        if command -v ldd >/dev/null; then
          ldd "$stage/squirrelsound.exe" | awk '/mingw64/ {print $3}' | sort -u | while read -r dll; do
            cp "$dll" "$stage/"
          done
        fi
        (cd "$(dirname "$stage")" && zip -qr "$out/$stem-$platform.zip" "$(basename "$stage")")
        echo "$out/$stem-$platform.zip" ;;
      macos-*)
        cat > "$stage/RUNTIME.txt" <<'TXT'
Needs SDL2 for the live view: brew install sdl2
The analysis commands (synth, spectrum, pitch, thd, spectrogram) work without it.
macOS may block the unsigned binary the first time: right-click it and choose Open,
or run: xattr -d com.apple.quarantine squirrelsound
TXT
        (cd "$(dirname "$stage")" && zip -qr "$out/$stem-$platform.zip" "$(basename "$stage")")
        echo "$out/$stem-$platform.zip" ;;
      *)
        cat > "$stage/RUNTIME.txt" <<'TXT'
Needs SDL2 and ALSA for the live view, e.g. on Debian/Ubuntu:
  sudo apt install libsdl2-2.0-0 libasound2
The analysis commands (synth, spectrum, pitch, thd, spectrogram) work without them.
TXT
        tar -C "$(dirname "$stage")" -czf "$out/$stem-$platform.tar.gz" "$(basename "$stage")"
        echo "$out/$stem-$platform.tar.gz" ;;
    esac ;;
esac
