# SquirrelSuica — Android app

Native Android app for **read-only Suica / PASMO-class FeliCa** balance and
history, plus a CameraX / ML Kit QR scanner.

Browsers cannot read FeliCa balances over Web NFC. This app uses `NfcF`.

## Features

- Hold Suica/PASMO → IDm, balance (¥), journey/charge history
- **Demo card** button (no NFC hardware required)
- **QR scan** activity (back camera)
- Read-only — no writes, no cloning

## Build (CLI)

Requires JDK 17+ and Android SDK 34.

```bash
export ANDROID_HOME=/path/to/android-sdk
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleDebug test
```

Debug APK:

```text
app/build/outputs/apk/debug/app-debug.apk
```

Install on a phone:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Android Studio

Open this `android/` folder as a project, sync Gradle, Run on a device with NFC.

## Package

`works.squirrel.suica` — version `1.0.0`
