#!/usr/bin/env bash
# Install host dependencies for `make test` / desktop packaging on CI runners.
# Usage: ci_host_deps.sh <linux|macos|windows>
# windows = run from an msys2 bash shell (UCRT64).
set -euo pipefail
os="${1:?usage: ci_host_deps.sh <linux|macos|windows>}"

case "$os" in
  linux)
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends \
      build-essential pkg-config \
      libsdl2-dev libsdl2-ttf-dev fonts-wqy-microhei fonts-droid-fallback \
      libasound2-dev libx11-dev xvfb xauth \
      libpcsclite-dev pcscd vsmartcard-vpcd libusb-1.0-0-dev \
      pulseaudio pulseaudio-utils \
      qemu-system-x86 nasm \
      dpkg-dev fakeroot curl ca-certificates \
      file desktop-file-utils
    ;;
  macos)
    brew update
    brew install sdl2 sdl2_ttf pkg-config libusb create-dmg || \
      brew install sdl2 sdl2_ttf pkg-config libusb
    ;;
  windows)
    # Caller has already set up MSYS2 UCRT64; install ucrt packages with pacman.
    # mingw64 no longer ships libusb; UCRT64 is the supported environment.
    pacman -S --noconfirm --needed \
      make zip git python diffutils \
      mingw-w64-ucrt-x86_64-gcc \
      mingw-w64-ucrt-x86_64-SDL2 \
      mingw-w64-ucrt-x86_64-SDL2_ttf \
      mingw-w64-ucrt-x86_64-pkgconf \
      mingw-w64-ucrt-x86_64-libusb \
      mingw-w64-ucrt-x86_64-libsystre
    ;;

  *)
    echo "unknown os: $os" >&2
    exit 2
    ;;
esac
