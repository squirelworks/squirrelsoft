# SquirrelSuica

Read-only **FeliCa card reader**: **Suica/PASMO family**, **nanaco**,
**楽天Edy**, and **WAON**. Journey history shows **station names**
(enter → exit) from the MIT-licensed Saibane station-code table
(`data/StationCode.csv`; see `data/README.md`). No writes, no cloning.

On Android the launcher label is **SquirrelSuica** (package `works.squirrel.suica`)
so it is easy to find next to SquirrelQR / SquirrelSound.

Parsers are covered by **fixture unit tests** (no physical card required).

## Paths

| Path | When to use |
|------|-------------|
| **`android/`** | Phone app (NFC-F), the preferred reader |
| `data/StationCode.csv` | Saibane region/line/station → name (+ note) table |
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
with station names, from a card on a PC/SC contactless reader or from a JSON
dump SquirrelSuicaAnalysis exports (replayed through a simulated card).

Live reads use either:

- **PC/SC** Part 3 transparent exchange for Sony's RC-S300 and other CCID
  contactless readers: pcsc-lite on Linux
  (`sudo apt install pcscd libpcsclite-dev`), the system PC/SC framework on
  macOS, winscard on Windows.
- **PaSoRi RC-S380 over USB** (`--usb`): an original libusb driver for the
  NFC Port-100 bulk protocol (`sudo apt install libusb-1.0-0-dev`). The RC-S380
  is not a CCID reader, so PC/SC cannot talk to it.

**GUI** (`--gui`): an optional SDL2 + SDL_ttf window shows brand, balance, and
scrollable history with station names (same view-model as the text CLI). Install
`libsdl2-dev libsdl2-ttf-dev` (and a Latin+CJK font such as
`fonts-wqy-microhei` on Debian/Ubuntu). Override the font with
`$SQUIRRELSUICA_FONT`. `--gui-ms MS` auto-quits after MS milliseconds for CI.

A build without PC/SC, libusb, or SDL headers still reads dumps; the matching
backend becomes a stub. Framing and InCommRF helpers are unit-tested without
hardware. **Checking a physical RC-S380 / live GUI is an owner-side post-merge
step**, not a CI or merge gate. Do not commit real card dumps.

```bash
make desktop
build/squirrelsuica                                  # wait 10 s for a card on the first PC/SC reader
build/squirrelsuica --list-readers                   # PC/SC names + usb:RC-S380 when plugged in
build/squirrelsuica --usb --wait 30 --json           # PaSoRi RC-S380 over libusb
build/squirrelsuica --reader RC-S300 --wait 30 --json
build/squirrelsuica --dump tests/fixtures/demo-suica.json          # made-up demo card, text
build/squirrelsuica --gui --dump tests/fixtures/demo-suica.json   # same dump in an SDL window
build/squirrelsuica --csv --dump tests/fixtures/demo-suica.json    # history CSV (IDm omitted)
build/squirrelsuica --csv-file out.csv --dump felica-dump.json     # write CSV to a file
build/squirrelsuica --dump felica-dump.json --json   # your own dump, JSON with felica_parse field names
make test-desktop                                    # C unit tests, parity with felica_parse.py, PC/SC test
```

CSV columns: `index,brand,system_code,card_balance_yen,date,time,kind,fare_yen,balance_yen,enter_station,exit_station,enter_code,exit_code,process_label`.
IDm/PMm are omitted unless you pass `--include-idm`. Do not commit real card dumps or exports that contain a real IDm.


Exit status: 0 read, 1 no card (or it did not answer), 2 reader or file error.
`make test-pcsc` reads a made-up card through pcscd and the vpcd virtual reader
(`sudo apt install pcscd vsmartcard-vpcd`); it is skipped when they are missing.

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
| Suica/PASMO/ICOCA/… | `0x0003` | `0x090f` bytes 10–11 LE (+ `0x008b` check) | **`0x090f`** (≤20 blocks) | Rail stations at bytes 6–9; bus uses operator/stop there; issuer label from `0x008b` byte 8 |
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

Station names come from the MIT-licensed Saibane table (see `data/README.md`).
Clean-room parsers — study open FeliCa history layouts; do not copy proprietary apps.

## Tests

```bash
make test
# Android fixtures (includes station-name + nanaco/Edy/WAON history cases):
cd android && ./gradlew :app:testDebugUnitTest
```
