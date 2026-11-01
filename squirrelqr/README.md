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
`0.1.4`). The previous identity `works.squirrel.field` is abandoned — create a
**new** Play app listing for `works.squirrel.qr`.

## Progressive web app (`web/`)

Static QR PWA (BarcodeDetector / image upload):

```bash
make serve   # http://0.0.0.0:8765/
```

| File | Purpose |
|------|---------|
| `web/index.html` | QR camera reader |
| `web/qr.js` | Detection loop + clipboard |

Use **HTTPS** or **localhost** so `getUserMedia` / `BarcodeDetector` work.
