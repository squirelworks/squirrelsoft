# SquirrelSuica — Android app

Native Android app for **read-only Suica / PASMO / nanaco / Edy / WAON FeliCa**
balance and history. NFC-F only — no camera, no QR, no other scanners.

Browsers cannot read FeliCa balances over Web NFC. This app uses `NfcF`.

## Features

- Hold a FeliCa purse card → IDm, balance (¥), journey/charge history
- Demo buttons for Suica / nanaco / Edy / WAON (no NFC hardware required)
- Reads only while the app is open (no TECH_DISCOVERED auto-launch)
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

`works.squirrel.suica` — version `0.0.2`
