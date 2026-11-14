# Desktop packaging (PKG / S3.5)

Every C desktop program in the top-level `Makefile` `PROGRAMS` list is packaged
by `scripts/release/package_desktop.sh` into:

| OS | Artifacts |
|---|---|
| Windows | `<Name>-<ver>-windows-x86_64.zip` and `.msi` when `msilib` is available (CPython ≤ 3.12 on the Windows runner) |
| macOS | `<Name>-<ver>-macos-arm64.dmg` containing `<Name>.app` (unsigned) |
| Linux | `<Name>-<ver>-linux-x86_64.AppImage` and `<Name>-<ver>-linux-amd64.deb` |

Unsigned packages are fine for now. No AABs, keystores or Play secrets are
involved in desktop packaging.

## Local dry run

```bash
scripts/release/app_meta.sh squirrelsound
scripts/release/package_desktop.sh squirrelsound /tmp/dist linux-x86_64
scripts/release/package_desktop.sh squirrelgnss /tmp/dist linux-x86_64
# macOS runners produce a real .dmg; elsewhere pkg_macos falls back to .app.zip
```

## CI

- `.github/workflows/ci.yml` runs `make test` on `ubuntu-latest`,
  `windows-latest` and `macos-latest` (S4).
- `.github/workflows/release.yml` does the same, then builds packages for every
  selected desktop program on all three OSes and attaches them to a **draft**
  GitHub Release. Choose app `desktop` to package the whole suite; Android app
  choices still build that app's APK plus its C desktop packages when it has one.

## What this unblocks

Windows and macOS runners (and installable packages) are the gate for:

- **SND-wasapi** / **SND-macos** — system-audio capture backends and their CI
- **QR-capture-win-mac** — screen capture on Windows and macOS
- **QR-wayland** — still Linux-only, but benefits from the same package path

Those features are intentionally not implemented in the PKG PR.

## Versions

Android-paired programs take `versionName` from `android/app/build.gradle.kts`.
Desktop-only programs read `<app>/VERSION`.
