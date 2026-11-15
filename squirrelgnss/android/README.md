# SquirrelGNSS — Android app

**Continuous satellite / GPS land surveying** (`works.squirrel.gnss`).

JNI bridge to the desktop `sqgnss` core: geodesy, stakeout, and a
**synthetic measurement feed** (fill packed observations → solve PVT) for CI.
Optional phone `GnssMeasurement` / `Location` listening when the user grants
location permission and GPS hardware is present. NTRIP corrections are not
wired yet.

- No microphone (see **SquirrelSound**)
- No QR camera (see **SquirrelQR**)
- No Suica / FeliCa (see **SquirrelSuica**)

## Build

```bash
export ANDROID_HOME=/path/to/android-sdk
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug test
# or: make -C .. android android-test
```

APK: `app/build/outputs/apk/debug/app-debug.apk`  
Package: `works.squirrel.gnss` — version `0.1.1`

Host JNI smoke (no SDK): `make -C .. test-jni`  
Instrumented (device/emulator): `./gradlew :app:connectedDebugAndroidTest`
