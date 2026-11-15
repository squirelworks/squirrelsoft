# SquirrelGNSS — Android app

**Continuous satellite / GPS land surveying** (`works.squirrel.gnss`).

First step: the desktop `sqgnss` core through JNI (geodesy, stakeout, synthetic
sky PVT). The host smoke under `jni-smoke/` exercises the bridge without an
Android SDK or phone GNSS. Phone `GnssMeasurement` and NTRIP are not wired yet.

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
Package: `works.squirrel.gnss` — version `0.1.0`

Host JNI smoke (no SDK): `make -C .. test-jni`
