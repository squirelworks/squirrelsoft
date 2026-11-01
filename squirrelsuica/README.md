# SquirrelSuica

Read-only **FeliCa** e-money reader for field use: **Suica/PASMO family**,
**nanaco**, **楽天Edy**, and **WAON**. No writes, no cloning.

You do **not** need every physical card to develop or test — parsers are covered
by **fixture unit tests** and **Demo** buttons with realistic ¥2–5k balances.

## Paths

| Path | When to use |
|------|-------------|
| **`android/`** | Phone app (NFC-F) |
| `suica_read.py` | USB PaSoRi via `nfcpy`, or `--demo suica\|nanaco\|edy\|waon` |
| `felica_parse.py` | Shared parsers + synthetic demos |
| `../squirrelfield/web/suica.html` | Browser UI demo (cannot read live balances) |

## Android

```bash
export ANDROID_HOME=/path/to/android-sdk
cd android
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew :app:assembleDebug :app:testDebugUnitTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Hold the card **still** on the NFC spot. If you see “Tag was lost”, the previous
balance is cleared so a bad read cannot linger. Demo buttons load synthetic
purses (not your real balance).

## Desktop / USB

```bash
python3 -m pip install --user nfcpy
python3 suica_read.py --demo nanaco
python3 suica_read.py --demo
python3 suica_read.py
```

## What is read (public layouts)

| Brand | System (typical) | Balance service | Layout |
|-------|------------------|-----------------|--------|
| Suica/PASMO/… | `0x0003` | `0x090f` (+ `0x008b` check) | LE u16 |
| nanaco | `0xFE00` / `0x04C7` | `0x5597` | LE u32 |
| 楽天Edy | `0xFE00` | `0x1317` | LE u32 |
| WAON | `0xFE00` | `0x6817` | LE u16 |

Unknown FeliCa: shows IDm and an unsupported-purse note (no invented balance).

## Tests

```bash
make test
# Android fixtures:
cd android && ./gradlew :app:testDebugUnitTest
```
