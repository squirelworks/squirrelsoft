# SquirrelQR

Phone **QR / barcode** reader — nothing else.

Spectrum analysis belongs in **SquirrelSound**. FeliCa transit cards belong in
**SquirrelSuica**. This package is intentionally specialized as a QR code
reader only (not a field kit).

## Android APK (`android/`)

Native CameraX + ML Kit scanner (`works.squirrel.qr`) with detection crosshairs
and a bottom-half history panel (open URL / copy / clear).

```bash
export ANDROID_HOME=/path/to/android-sdk
make android
make android-test
# APK: android/app/build/outputs/apk/debug/app-debug.apk
```

**Play Console note:** `applicationId` is `works.squirrel.qr` (version
`0.1.5`). The previous identity `works.squirrel.field` is abandoned — create a
**new** Play app listing for `works.squirrel.qr`.

## Desktop reader (`desktop/`)

A command-line reader on Squirrel's own decoder (`libs/sqqr`), no ML Kit.
It decodes image files, or a rectangle of the screen on X11 (Linux and BSD
desktops; Wayland, Windows and macOS capture come later in roadmap S3.4).

```bash
make desktop                         # build/squirrelqr (X11 capture when libx11-dev is installed)
build/squirrelqr shot.png code.pgm   # PNG, PGM/PPM/PBM and BMP files; one line of text per code
build/squirrelqr --select            # drag a rectangle with the mouse; Esc cancels
build/squirrelqr --screen --wait 10  # keep looking at the whole screen for up to 10 s
build/squirrelqr --region 0,0,800,600 --json
make test-desktop                    # unit + CLI tests; the capture test runs under xvfb-run
```

`--json` prints the text, the charset it was read as, the symbol version and
the corners in screen or image coordinates. Text follows the Android app's
rules: an ECI wins, then UTF-8, then Shift JIS (through iconv), then
ISO-8859-1. On-screen codes need about 3 pixels per module; small ones are
also tried at 2x.

PNG files are read by SquirrelQR's own decoder (no libpng or zlib): every
colour type and bit depth, interlaced files, and transparency, which is
composited over white so a code saved on a transparent background reads like
it does on a page.

**Wayland:** the X11 capture only sees XWayland windows. Until the Wayland
screenshot portal is supported, take a screenshot with the desktop's own tool
(GNOME and KDE screenshot, or `grim -g "$(slurp)" shot.png` on wlroots
compositors) and pass the PNG: `build/squirrelqr shot.png`.

## Web demo (`web-demo/`), not a product

SquirrelQR ships as native apps: the Android app here and a desktop app
(roadmap S3.4), both on Squirrel's own decoder in `libs/sqqr`. The old browser
PWA is kept in `web-demo/` only as a demo of the browser's BarcodeDetector API.
It isn't released, packaged or supported. See `web-demo/README.md`.

```bash
make serve   # http://0.0.0.0:8765/ (HTTPS or localhost needed for the camera)
```
