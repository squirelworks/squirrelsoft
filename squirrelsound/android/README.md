# SquirrelSound — Android app

Native phone spectrum analyzer only: **AudioRecord** mic (UNPROCESSED/MIC,
with fallback) or demo 440 Hz → windowed radix-2 FFT → spectrum trace +
scrolling waterfall. Pinch to zoom frequency; max-hold overlay.

No QR. No NFC. No Field hub. This APK is the Spectroid-class sound tool.

Pairs with the desktop `squirrelsound live` CLI (SDL/ALSA).

## Build

```bash
export ANDROID_HOME=/path/to/android-sdk
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug test
# or: make -C .. android android-test
```

APK: `app/build/outputs/apk/debug/app-debug.apk`  
Package: `works.squirrel.sound` — version `0.0.2`
