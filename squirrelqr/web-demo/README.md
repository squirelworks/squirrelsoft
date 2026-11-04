# SquirrelQR web demo (not a product)

This is the old SquirrelQR browser app: a camera page that uses the browser's
`BarcodeDetector` API, with an image-upload fallback. It is kept only as a demo.
It isn't released, packaged, hosted or supported, and it doesn't use Squirrel's
own decoder.

SquirrelQR itself is a native app:

- Android: `../android/` (CameraX, ML Kit, and `libs/sqqr` through JNI)
- Desktop: planned (roadmap S3.4), on `libs/sqqr`

To try the demo locally:

```bash
make -C .. serve   # http://0.0.0.0:8765/
```

The camera needs HTTPS or localhost, and `BarcodeDetector` is only in some
browsers (Chromium-based ones on Android and macOS, for example).
