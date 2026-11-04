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

## Web demo (`web-demo/`), not a product

SquirrelQR ships as native apps: the Android app here and a desktop app
(roadmap S3.4), both on Squirrel's own decoder in `libs/sqqr`. The old browser
PWA is kept in `web-demo/` only as a demo of the browser's BarcodeDetector API.
It isn't released, packaged or supported. See `web-demo/README.md`.

```bash
make serve   # http://0.0.0.0:8765/ (HTTPS or localhost needed for the camera)
```
