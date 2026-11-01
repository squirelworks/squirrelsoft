# SquirrelSuica

Suica / PASMO-class **FeliCa** transit IC reader for land-survey and daily
field use. Read-only: IDm, balance, and journey history blocks.

## Paths

| Path | When to use |
|------|-------------|
| **`android/`** | **Phone app (FeliCa NFC-F)** — install the APK |
| `suica_read.py` | USB NFC reader (Sony PaSoRi RC-S380/RC-S300, etc.) via `nfcpy` |
| `--demo` | No hardware — canned card for UI/tests |
| `../squirrelfield/web/suica.html` | Browser demo / NDEF probe (cannot read Suica balance) |

## Android phone app (recommended)

Full Gradle project under `android/`:

```bash
export ANDROID_HOME=/path/to/android-sdk
cd android
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew :app:assembleDebug :app:testDebugUnitTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Or open `android/` in Android Studio → Run.

App id: `works.squirrel.suica` — hold Suica/PASMO to read balance/history, or tap **Demo card**. Includes a QR scanner.

## Desktop / USB

```bash
python3 -m pip install --user nfcpy
python3 suica_read.py --demo
python3 suica_read.py            # waits for card on USB reader
python3 suica_read.py --json
```

## What is read

Publicly documented FeliCa transit services (system code `0x0003`):

- Balance service `0x090f`
- History-style blocks commonly used by open Suica readers

No writes, no cloning, no payment cryptography bypass.

## Tests

```bash
make test
```
