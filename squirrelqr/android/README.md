# SquirrelQR — Android app

**QR code reader** (`works.squirrel.qr`).

- CameraX + ML Kit continuous scan
- Copy to clipboard / clear for next code
- No spectrum (see **SquirrelSound**)
- No Suica / FeliCa (see **SquirrelSuica**)
- No field-kit / multi-tool hub

## Build

```bash
export ANDROID_HOME=/path/to/android-sdk
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug test
# or: make -C .. android android-test
```

APK: `app/build/outputs/apk/debug/app-debug.apk`  
Package: `works.squirrel.qr` — version `0.1.0`

## Play Console

Changing `applicationId` from `works.squirrel.field` to `works.squirrel.qr`
creates a **new** Play app identity. Publish under a new listing; do not try to
update the abandoned `works.squirrel.field` package.
