# SquirrelField

Phone **QR / barcode** reader for field work.

Spectrum analysis belongs in **SquirrelSound**. FeliCa transit cards belong in
**SquirrelSuica**. This package is intentionally specialized.

## Android APK (`android/`)

Native CameraX + ML Kit scanner (`works.squirrel.field`).

```bash
export ANDROID_HOME=/path/to/android-sdk
make android
make android-test
# APK: android/app/build/outputs/apk/debug/app-debug.apk
```

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
