# SquirrelQR — Android app

**QR code reader** (`works.squirrel.qr`).

- CameraX + ML Kit continuous scan with bounding-box crosshair overlay
- Bottom half: last result, scan history, open URL / copy, clear history
- WindowInsets padding so controls clear the system nav / home gesture area
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
Package: `works.squirrel.qr` — version `0.1.4`

## Play Console

Changing `applicationId` from `works.squirrel.field` to `works.squirrel.qr`
creates a **new** Play app identity. Publish under a new listing; do not try to
update the abandoned `works.squirrel.field` package.
