# SquirrelSuica

Read-only **FeliCa card reader**: **Suica/PASMO family**, **nanaco**,
**楽天Edy**, and **WAON**. Journey history shows **station names**
(enter → exit) from the public Saibane station-code table. No writes, no cloning.

On Android the launcher label is **SquirrelSuica** (package `works.squirrel.suica`)
so it is easy to find next to SquirrelQR / SquirrelSound.

Parsers are covered by **fixture unit tests** (no physical card required).

## Paths

| Path | When to use |
|------|-------------|
| **`android/`** | Phone app (NFC-F), the preferred reader |
| `data/StationCode.csv` | Saibane region/line/station → name table |
| `suica_read.py` | USB PaSoRi via `nfcpy`, or `--demo suica\|nanaco\|edy\|waon` |
| `felica_parse.py` | Shared parsers + synthetic fixtures |

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
hex.

## Desktop reader in C (`desktop/`)

`build/squirrelsuica` is the native desktop reader on the shared FeliCa core
(`libs/sqfelica`), the same parsing as the apps. It reads balance and history
with station names. For now it reads the JSON dumps SquirrelSuicaAnalysis
exports, by replaying them through a simulated card. Live readers (PC/SC, Sony
PaSoRi over USB) are the next step of roadmap S3.3.

```bash
make desktop
build/squirrelsuica --dump felica-dump.json          # text
build/squirrelsuica --dump felica-dump.json --json   # JSON, felica_parse field names
make test-desktop                                    # C unit tests + parity with felica_parse.py
```

## Desktop / USB (Python, nfcpy)

```bash
python3 -m pip install --user nfcpy
python3 suica_read.py --demo nanaco
python3 suica_read.py --demo
python3 suica_read.py
```

## What is read (public layouts)

| Brand | System (typical) | Balance | History | Notes |
|-------|------------------|---------|---------|-------|
| Suica/PASMO/… | `0x0003` | `0x090f` bytes 10–11 LE (+ `0x008b` check) | **`0x090f`** (≤20 blocks) | Stations at bytes 6–9; region at 15 |
| nanaco | `0xFE00` / `0x04C7` | `0x5597` LE u32 | `0x564f` (5 blocks) | Amount/balance BE in history |
| 楽天Edy | `0xFE00` | `0x1317` LE u32 | `0x170f` (6 blocks) | Timestamp days+seconds since 2000 |
| WAON | `0xFE00` | `0x6817` LE u16 | `0x680b` (3×32-byte records) | Bit-packed date/amount |

Unknown FeliCa: shows IDm and an unsupported-purse note (no invented balance).

**Bug fixed in 0.0.5:** history was incorrectly read from service `0x010f`
(empty/wrong). Suica history is service `0x090f`. Balance in history blocks is
at bytes **10–11**, not 0–1.

**Bug fixed in 0.0.6:** History section could stay blank/`(empty)` even when
balance worked. Phone readers now follow the public SuicaReader-style path —
`0x090f` up to 20 blocks, multi-block with **single-block fallback**, longer
NFC timeout, and actionable History errors when the tag is lost mid-read.
Rows show date, process, **station names**, fare (balance delta), and balance.

**Bug fixed in 0.0.11:** nanaco cards were dropped before anything was read.
The reader polled Suica's system `0x0003` first; a nanaco card (system
`0x04C7` only) stays silent, and Android reports that silence as a lost tag.
The reader now asks the card for its system codes (Request System Code, or
the system 0 code from a wildcard poll) and only polls systems it has.

Station names come from the community SFCardFan サイバネ表 (see `data/README.md`).
Clean-room parsers — study open FeliCa history layouts; do not copy proprietary apps.

## Tests

```bash
make test
# Android fixtures (includes station-name + nanaco/Edy/WAON history cases):
cd android && ./gradlew :app:testDebugUnitTest
```
