# SquirrelSound — Android app

Native phone spectrum analyzer only: **AudioRecord** mic (UNPROCESSED/MIC,
with fallback) or demo 440 Hz → hopped windowed radix-2 FFT → spectrum trace +
scrolling waterfall bitmap. Pinch to zoom frequency; max-hold overlay.

Capture is continuous; FFT hops on a Spectroid-like interval (~20 ms) with
exponential smoothing and dBFS scaling. Status shows mic path, peak Hz/dB, and
RMS so a live mic is visually distinct from silence or failure (no silent demo
fallback when the mic was requested).

No QR. No NFC. No multi-tool hub. This APK is the Spectroid-class sound tool.

Pairs with the desktop `squirrelsound live` CLI (SDL/ALSA).

## Build

```bash
export ANDROID_HOME=/path/to/android-sdk
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug test
# or: make -C .. android android-test
```

APK: `app/build/outputs/apk/debug/app-debug.apk`  
Package: `works.squirrel.sound` — version `0.0.7`

Capture stays alive through transient `onPause` (notification shade); the mic is
released in `onStop` when leaving the app. AudioRecord read errors restart the
session; a partial wake lock + media audio focus reduce OEM mic kill while the
spectrum screen is visible. Capture sessions are generation-scoped so stop/restart
cannot leave a stuck READ_BLOCKING thread double-writing the UI, and permission /
mic-busy failures surface as status text instead of crashing.
