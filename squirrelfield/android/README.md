# SquirrelField — Android app

**QR / barcode camera reader only** (`works.squirrel.field`).

- CameraX + ML Kit continuous scan
- Copy to clipboard / clear for next code
- No spectrum (see **SquirrelSound**)
- No Suica / FeliCa (see **SquirrelSuica**)
- No bundled WebView hub

## Build

```bash
export ANDROID_HOME=/path/to/android-sdk
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug test
# or: make -C .. android android-test
```

APK: `app/build/outputs/apk/debug/app-debug.apk`  
Package: `works.squirrel.field` — version `0.0.2`
