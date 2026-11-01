# SquirrelSuica

Suica / PASMO-class **FeliCa** transit IC reader for land-survey and daily
field use. Read-only: IDm, balance, and journey history blocks.

## Paths

| Path | When to use |
|------|-------------|
| `suica_read.py` | USB NFC reader (Sony PaSoRi RC-S380/RC-S300, etc.) via `nfcpy` |
| `--demo` | No hardware — canned card for UI/tests |
| `android/` | Phone NFC-F companion (Android) |
| `../squirrelfield/web/suica.html` | Phone UI + demo / Web NFC NDEF probe |

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
