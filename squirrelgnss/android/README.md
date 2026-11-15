# SquirrelGNSS — Android app

**Continuous satellite / GPS land surveying** (`works.squirrel.gnss`).

JNI bridge to the desktop cores (`sqgnss`, `sqntrip`, `sqrtcm`): geodesy,
stakeout, a **synthetic measurement feed** (fill packed observations → solve
PVT) for CI, and **NTRIP → RTCM MSM** (memio synth / `127.0.0.1` loopback in
CI; optional operator dial). Optional phone `GnssMeasurement` / `Location`
listening when the user grants location permission and GPS hardware is
present. See `docs/android-ntrip.md`.

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
(covers geodesy, synthetic PVT, and NTRIP memio + loopback → MSM4)  
Instrumented (device/emulator): `./gradlew :app:connectedDebugAndroidTest`
