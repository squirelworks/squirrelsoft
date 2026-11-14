# SquirrelQR

Phone **QR / barcode** reader — nothing else.

Spectrum analysis belongs in **SquirrelSound**. FeliCa transit cards belong in
**SquirrelSuica**. This package is intentionally specialized as a QR code
reader only (not a field kit).

## Android APK (`android/`)

Native CameraX + ML Kit scanner (`works.squirrel.qr`) with detection crosshairs
and a bottom-half history panel (open URL / copy / clear). Squirrel's own
decoder (`libs/sqqr`, through JNI) reads the frames ML Kit misses.

- **What a code means:** under the last read, Wi-Fi networks, contacts,
  e-mail, phone, SMS, locations and links are shown as fields (the same
  `sqqr_content` parser as `squirrelqr --details`).
- **Make QR:** type text or a link, or fill in a Wi-Fi network (name,
  password, security, hidden), pick the error correction level, and the app
  draws the code with Squirrel's encoder (`sqqr_encode`). Share it, or save
  it as `squirrelqr-YYYYMMDD-HHMMSS.png`.

```bash
export ANDROID_HOME=/path/to/android-sdk
make android
make android-test
# APK: android/app/build/outputs/apk/debug/app-debug.apk
```

`make test` also builds the JNI bridge (`android/app/src/main/cpp/sqqr_jni.c`)
for this machine and drives every native entry point from a JVM
(`android/jni-smoke/`), so the bridge is tested without an Android SDK. It is
skipped when no JDK is installed.

**Play Console note:** `applicationId` is `works.squirrel.qr` (version
`0.1.5`). The previous identity `works.squirrel.field` is abandoned — create a
**new** Play app listing for `works.squirrel.qr`.

## Desktop reader (`desktop/`)

A command-line reader on Squirrel's own decoder (`libs/sqqr`), no ML Kit.
It decodes image files, a rectangle of the screen (X11 or xdg-desktop-portal
on Linux/Wayland, GDI/DXGI on Windows, ScreenCaptureKit on macOS), or a
webcam frame (V4L2 on Linux; Media Foundation / AVFoundation stubs on
Windows and macOS).

```bash
make desktop                         # build/squirrelqr (portal+X11 + V4L2 when headers are installed)
build/squirrelqr shot.png code.pgm   # PNG, PGM/PPM/PBM and BMP files; one line of text per code
build/squirrelqr --select            # drag a rectangle with the mouse; Esc cancels
build/squirrelqr --screen --wait 10  # keep looking at the whole screen for up to 10 s
build/squirrelqr --region 0,0,800,600 --json
build/squirrelqr --webcam            # one frame from /dev/video0 (or --device PATH)
build/squirrelqr --webcam --wait 15  # keep grabbing until a code shows up
build/squirrelqr devices             # list V4L2 capture devices
build/squirrelqr --webcam --device @shot.png   # synthetic frame (CI; no camera)
QR_WEBCAM_FRAME=shot.png build/squirrelqr --webcam
QR_CAPTURE_FRAME=shot.png build/squirrelqr --select   # synthetic screen (CI; no display)
build/squirrelqr --gui [shot.png]    # small window: Select area, Whole screen, open a file, Copy text, Make QR
build/squirrelqr --details shot.png  # also say what it is (Wi-Fi, contact, link, ...) and list its fields
build/squirrelqr --encode "https://squirrel.works/"                 # draw a code in the terminal
build/squirrelqr --encode "hello" --level H --out hello.png         # or write .png / .svg / .pgm
build/squirrelqr --wifi "Nest" --password "acorns" --out wifi.png   # a Wi-Fi code phones can join from
make test-desktop                    # unit + CLI + webcam/capture synthetic tests; X11 under xvfb-run
```

Codes are made by `libs/sqqr`'s own encoder (one segment in the most compact
mode, the smallest version for the level, the lowest-penalty mask; checked
module for module against an independent encoder). Reading understands the
common payload conventions through `sqqr_content` in the same library: Wi-Fi
(`WIFI:`), contacts (vCard, MECARD), e-mail (`mailto:`, `MATMSG:`), phone,
SMS, `geo:` locations and web links. `--json` adds `"type"` and `"fields"`.

The `--gui` window (`libs/sqgui` on SDL2) shows the last image with the
code outlined in green, the decoded text, and the version and charset in
the status line. Copy puts the text on the clipboard.

`--json` prints the text, the charset it was read as, the symbol version and
the corners in screen or image coordinates. Text follows the Android app's
rules: an ECI wins, then UTF-8, then Shift JIS (through iconv), then
ISO-8859-1. On-screen codes need about 3 pixels per module; small ones are
also tried at 2x.

PNG files are read by SquirrelQR's own decoder (no libpng or zlib): every
colour type and bit depth, interlaced files, and transparency, which is
composited over white so a code saved on a transparent background reads like
it does on a page.

**Wayland:** when `WAYLAND_DISPLAY` is set, `--select` / `--screen` use the
xdg-desktop-portal Screenshot API (GNOME and other portal compositors). On X11
sessions the X11 back end is used instead. Both are linked in a typical Linux
build (`CAPTURE=linux`).

## Web demo (`web-demo/`), not a product

SquirrelQR ships as native apps: the Android app here and a desktop app
(roadmap S3.4), both on Squirrel's own decoder in `libs/sqqr`. The old browser
PWA is kept in `web-demo/` only as a demo of the browser's BarcodeDetector API.
It isn't released, packaged or supported. See `web-demo/README.md`.

```bash
make serve   # http://0.0.0.0:8765/ (HTTPS or localhost needed for the camera)
```
