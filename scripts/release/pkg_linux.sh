#!/usr/bin/env bash
# Package a staged Linux binary tree as AppImage + .deb into <outdir>.
# Usage: pkg_linux.sh <stage-dir> <display-name> <binary-name> <version> <outdir>
set -euo pipefail
stage="${1:?}"; name="${2:?}"; binary="${3:?}"; version="${4:?}"; out="${5:?}"
out="$(cd "$out" && pwd)"
stage="$(cd "$stage" && pwd)"
stem="$name-$version"
arch=x86_64
deb_arch=amd64
workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

# --- .deb -----------------------------------------------------------------
deb_root="$workdir/deb"
mkdir -p "$deb_root/DEBIAN" "$deb_root/usr/bin" "$deb_root/usr/share/doc/$binary"
install -m 755 "$stage/$binary" "$deb_root/usr/bin/$binary"
# Extra staged files (README, data, DLLs that are not the binary) go under share.
for f in "$stage"/*; do
  base="$(basename "$f")"
  [ "$base" = "$binary" ] && continue
  if [ -d "$f" ]; then
    cp -a "$f" "$deb_root/usr/share/doc/$binary/"
  else
    install -m 644 "$f" "$deb_root/usr/share/doc/$binary/"
  fi
done
# Direct DT_NEEDED only (ldd is transitive and over-states Depends).
needed=""
if command -v readelf >/dev/null; then
  needed=$(readelf -d "$stage/$binary" 2>/dev/null | awk '/NEEDED/ {gsub(/[\[\]]/,"",$5); print $5}')
elif command -v objdump >/dev/null; then
  needed=$(objdump -p "$stage/$binary" 2>/dev/null | awk '/NEEDED/ {print $2}')
fi
depends="libc6"
echo "$needed" | grep -qi 'libSDL2-2\.0\.so' && depends="$depends, libsdl2-2.0-0"
echo "$needed" | grep -qi 'libSDL2_ttf' && depends="$depends, libsdl2-ttf-2.0-0"
echo "$needed" | grep -qi 'libasound\.so' && depends="$depends, libasound2"
echo "$needed" | grep -qi 'libpcsclite\.so' && depends="$depends, libpcsclite1"
echo "$needed" | grep -qi 'libusb-1\.0\.so' && depends="$depends, libusb-1.0-0"
echo "$needed" | grep -qi 'libX11\.so' && depends="$depends, libx11-6"
cat > "$deb_root/DEBIAN/control" <<EOF
Package: $binary
Version: $version
Section: utils
Priority: optional
Architecture: $deb_arch
Maintainer: SquirrelWorks <squirrel@squirelworks.github.io>
Depends: $depends
Description: $name
 Original SquirrelSoft desktop program ($name).
 Unsigned CI package (S3.5 / PKG).
EOF
printf 'Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\nUpstream-Name: %s\nFiles: *\nCopyright: SquirrelWorks\nLicense: GPL-3.0-or-later\n' "$name" \
  > "$deb_root/usr/share/doc/$binary/copyright"
dpkg-deb --build --root-owner-group "$deb_root" "$out/$stem-linux-$deb_arch.deb"
echo "$out/$stem-linux-$deb_arch.deb"

# --- AppImage -------------------------------------------------------------
appdir="$workdir/$name.AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
install -m 755 "$stage/$binary" "$appdir/usr/bin/$binary"
for f in "$stage"/*; do
  base="$(basename "$f")"
  [ "$base" = "$binary" ] && continue
  cp -a "$f" "$appdir/usr/bin/" 2>/dev/null || true
done
cat > "$appdir/$binary.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$name
Exec=$binary
Icon=$binary
Categories=Utility;
Terminal=false
EOF
cp "$appdir/$binary.desktop" "$appdir/usr/share/applications/"
# Minimal placeholder PNG (1x1) so appimagetool accepts the dir; real icons later.
python3 - <<'PY' "$appdir/$binary.png" "$appdir/usr/share/icons/hicolor/256x256/apps/$binary.png"
import struct, sys, zlib
def png(path):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b'\x00' + b'\x00\x00\x00'  # 1x1 black RGB
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)
for p in sys.argv[1:]:
    png(p)
PY
cat > "$appdir/AppRun" <<EOF
#!/bin/sh
HERE="\$(dirname "\$(readlink -f "\$0")")"
export PATH="\$HERE/usr/bin:\$PATH"
exec "\$HERE/usr/bin/$binary" "\$@"
EOF
chmod +x "$appdir/AppRun"

tool="$workdir/appimagetool"
if [ ! -x "$tool" ]; then
  url="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
  if command -v curl >/dev/null; then
    curl -fsSL -o "$tool" "$url" || true
  elif command -v wget >/dev/null; then
    wget -q -O "$tool" "$url" || true
  fi
  chmod +x "$tool" 2>/dev/null || true
fi

appimage_out="$out/$stem-linux-$arch.AppImage"
if [ -x "$tool" ]; then
  (
    cd "$workdir"
    export ARCH="$arch"
    export APPIMAGE_EXTRACT_AND_RUN=1
    if "$tool" "$appdir" "$appimage_out" 2>"$workdir/ai.err"; then
      echo "$appimage_out"
    else
      echo "appimagetool failed; writing a portable AppDir tarball instead" >&2
      cat "$workdir/ai.err" >&2 || true
      tar -C "$workdir" -czf "$out/$stem-linux-$arch.AppDir.tar.gz" "$(basename "$appdir")"
      # Still emit a runnable pseudo-AppImage: self-extracting shell + tar.
      {
        printf '#!/bin/sh\n# Portable fallback when appimagetool is unavailable.\n'
        printf 'set -e\nTMP=$(mktemp -d)\nARCHIVE=$(awk "/^__ARCHIVE__/ {print NR+1; exit 0}" "$0")\n'
        printf 'tail -n+"$ARCHIVE" "$0" | tar -xz -C "$TMP"\n'
        printf 'exec "$TMP/%s/AppRun" "$@"\n__ARCHIVE__\n' "$(basename "$appdir")"
        tar -C "$workdir" -cz "$(basename "$appdir")"
      } > "$appimage_out"
      chmod +x "$appimage_out"
      echo "$appimage_out"
    fi
  )
else
  echo "appimagetool not downloaded; writing portable AppImage shell bundle" >&2
  {
    printf '#!/bin/sh\nset -e\nTMP=$(mktemp -d)\nARCHIVE=$(awk "/^__ARCHIVE__/ {print NR+1; exit 0}" "$0")\n'
    printf 'tail -n+"$ARCHIVE" "$0" | tar -xz -C "$TMP"\n'
    printf 'exec "$TMP/%s/AppRun" "$@"\n__ARCHIVE__\n' "$(basename "$appdir")"
    tar -C "$workdir" -cz "$(basename "$appdir")"
  } > "$appimage_out"
  chmod +x "$appimage_out"
  echo "$appimage_out"
fi
