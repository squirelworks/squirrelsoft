# Suica Reader (SquirrelSuica) — Android app

Native Android **Suica Reader** for **read-only Suica / PASMO / nanaco / Edy / WAON
FeliCa** balance and journey history with **station names**. NFC-F only — no
camera, no QR, no other scanners.

Launcher label: **Suica Reader** · package `works.squirrel.suica` · version `0.0.3`.

Browsers cannot read FeliCa balances over Web NFC. This app uses `NfcF`.

## Features

- Hold a FeliCa purse card → IDm, balance (¥), journey/charge history
- History shows **enter → exit station names** when Saibane codes resolve
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

## Station names

`StationCode.csv` (classpath resource) maps region/line/station codes from
history service `0x090f` to Japanese names. See `../data/README.md`.
