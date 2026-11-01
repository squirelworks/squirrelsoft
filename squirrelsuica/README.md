# SquirrelSuica — Suica Reader

Read-only **FeliCa Suica Reader** for field use: **Suica/PASMO family**,
**nanaco**, **楽天Edy**, and **WAON**. Journey history shows **station names**
(enter → exit) from the public Saibane station-code table. No writes, no cloning.

On Android the launcher label is **Suica Reader** (package `works.squirrel.suica`)
so it is easy to find next to unrelated Field/Sound apps.

You do **not** need every physical card to develop or test — parsers are covered
by **fixture unit tests** and **Demo** buttons with realistic ¥2–5k balances.

## Paths

| Path | When to use |
|------|-------------|
| **`android/`** | Phone app (NFC-F) — preferred Suica Reader |
| `data/StationCode.csv` | Saibane region/line/station → name table |
| `suica_read.py` | USB PaSoRi via `nfcpy`, or `--demo suica\|nanaco\|edy\|waon` |
| `felica_parse.py` | Shared parsers + synthetic demos |
| `../squirrelfield/web/` | Field tools (not the Suica Reader) |

## Android

```bash
export ANDROID_HOME=/path/to/android-sdk
cd android
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew :app:assembleDebug :app:testDebugUnitTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Hold the card **still** on the NFC spot **with the app open**. NFC is registered
only in the foreground (no `TECH_DISCOVERED` launch filter), so a card in your
phone flap will not auto-open the app.

History rows show date, kind, fare (from balance delta), balance, and
**station names** when the codes resolve. Unknown codes fall back to `LINE/STA`
hex. Demo Suica includes 乃木坂→代々木上原 and 渋谷→新宿 samples.

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
| Suica/PASMO/… | `0x0003` | `0x090f` (+ `0x008b` check) | LE u16; history stations at bytes 6–9 |
| nanaco | `0xFE00` / `0x04C7` | `0x5597` | LE u32 |
| 楽天Edy | `0xFE00` | `0x1317` | LE u32 |
| WAON | `0xFE00` | `0x6817` | LE u16 |

Unknown FeliCa: shows IDm and an unsupported-purse note (no invented balance).

Station names come from the community SFCardFan サイバネ表 (see `data/README.md`).
Clean-room parsers — study open FeliCa history layouts; do not copy proprietary apps.

## Tests

```bash
make test
# Android fixtures (includes station-name cases):
cd android && ./gradlew :app:testDebugUnitTest
```
