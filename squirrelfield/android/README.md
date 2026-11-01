# SquirrelField — Android app

Phone field toolkit APK:

- **Live spectrum** — bundled PWA (`web/spectrum.html`) in WebView with mic permission
- **QR reader** — CameraX + ML Kit (native)
- **Suica** — launches `works.squirrel.suica` when installed; demo web UI otherwise
- **Field hub** — full bundled PWA index

## Build

```bash
export ANDROID_HOME=/path/to/android-sdk
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug test
# or: make -C .. android android-test
```

APK: `app/build/outputs/apk/debug/app-debug.apk`  
Package: `works.squirrel.field`

Bundled assets under `app/src/main/assets/web/` (copy of `../web/`).
