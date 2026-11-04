# Suica Reader (SquirrelSuica) — Android app

Native Android **Suica Reader** for **read-only Suica / PASMO / nanaco / Edy / WAON
FeliCa** balance and journey history with **station names**. NFC-F only — no
camera, no QR, no other scanners.

Launcher label: **Suica Reader** · package `works.squirrel.suica` · version `0.0.9`.

Browsers cannot read FeliCa balances over Web NFC. This app uses `NfcF`.

## Features

- Hold a FeliCa purse card → IDm, balance (¥), journey/charge history
- History shows **enter → exit station names**, fare, and balance
- Suica history from service **`0x090f`** (≤20 blocks; single-block fallback)
- Failed history reads show an actionable error — never a blank `(empty)` list
- nanaco / Edy / WAON history when the purse is present
- Reads only while the app is open (no TECH_DISCOVERED auto-launch)
- Read-only — no writes, no cloning
- Fixture unit tests cover parsers (no physical card required)

## Build (CLI)

Requires JDK 17+ and Android SDK 34+.

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
