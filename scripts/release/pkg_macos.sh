#!/usr/bin/env bash
# Package a staged macOS binary as Name.app inside a .dmg.
# Usage: pkg_macos.sh <stage-dir> <display-name> <binary-name> <version> <outdir>
set -euo pipefail
stage="${1:?}"; name="${2:?}"; binary="${3:?}"; version="${4:?}"; out="${5:?}"
out="$(cd "$out" && pwd)"
stage="$(cd "$stage" && pwd)"
stem="$name-$version"
workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

app="$workdir/$name.app"
macos="$app/Contents/MacOS"
res="$app/Contents/Resources"
fw="$app/Contents/Frameworks"
mkdir -p "$macos" "$res" "$fw"
install -m 755 "$stage/$binary" "$macos/$binary"
for f in "$stage"/*; do
  base="$(basename "$f")"
  [ "$base" = "$binary" ] && continue
  if [ -d "$f" ]; then
    cp -a "$f" "$res/"
  else
    case "$base" in
      *.dylib|*.so) cp -a "$f" "$fw/" ;;
      *) cp -a "$f" "$res/" ;;
    esac
  fi
done

# Bundle non-system dylibs the binary links (Homebrew SDL2, etc.).
if command -v otool >/dev/null; then
  # Copy direct deps that live outside /usr/lib and /System.
  while read -r lib; do
    [ -z "$lib" ] && continue
    case "$lib" in
      /usr/lib/*|/System/*|@*) continue ;;
    esac
    [ -f "$lib" ] || continue
    base="$(basename "$lib")"
    cp -f "$lib" "$fw/$base"
    chmod u+w "$fw/$base" 2>/dev/null || true
    install_name_tool -change "$lib" "@executable_path/../Frameworks/$base" "$macos/$binary" 2>/dev/null || true
  done < <(otool -L "$macos/$binary" | awk 'NR>1 {print $1}')
  # Second pass: fix ids and transitive deps inside Frameworks.
  for dylib in "$fw"/*.dylib; do
    [ -f "$dylib" ] || continue
    base="$(basename "$dylib")"
    install_name_tool -id "@executable_path/../Frameworks/$base" "$dylib" 2>/dev/null || true
    while read -r lib; do
      [ -z "$lib" ] && continue
      case "$lib" in
        /usr/lib/*|/System/*|@*) continue ;;
      esac
      [ -f "$lib" ] || continue
      dep="$(basename "$lib")"
      if [ ! -f "$fw/$dep" ]; then
        cp -f "$lib" "$fw/$dep"
        chmod u+w "$fw/$dep" 2>/dev/null || true
        install_name_tool -id "@executable_path/../Frameworks/$dep" "$fw/$dep" 2>/dev/null || true
      fi
      install_name_tool -change "$lib" "@executable_path/../Frameworks/$dep" "$dylib" 2>/dev/null || true
    done < <(otool -L "$dylib" | awk 'NR>1 {print $1}')
  done
fi

cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>$name</string>
  <key>CFBundleDisplayName</key><string>$name</string>
  <key>CFBundleIdentifier</key><string>works.squirrel.$binary</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundleExecutable</key><string>$binary</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
printf 'APPL????' > "$app/Contents/PkgInfo"

dmg="$out/$stem-macos-arm64.dmg"
if command -v hdiutil >/dev/null; then
  vol="$workdir/dmgroot"
  mkdir -p "$vol"
  cp -a "$app" "$vol/"
  ln -s /Applications "$vol/Applications" 2>/dev/null || true
  hdiutil create -volname "$name" -srcfolder "$vol" -ov -format UDZO "$dmg"
else
  # Non-macOS fallback for local dry runs: zip the .app (CI macOS has hdiutil).
  dmg="${dmg%.dmg}.app.zip"
  (cd "$workdir" && zip -qr "$dmg" "$name.app")
fi
echo "$dmg"
