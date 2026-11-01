# SquirrelSuica Android (NFC-F)

Minimal Android companion to read Suica/PASMO-class FeliCa cards on a phone.

## Why native Android?

Chrome Web NFC is NDEF-oriented and typically **cannot** read FeliCa transit
balances. Android `NfcF` can.

## Import

1. Open Android Studio → Open this `android/` folder (or copy `app/` into a new project).
2. Build the `app` module, install on a device with NFC.
3. Hold a Suica/PASMO to the back of the phone.

## Source

- `app/src/main/java/works/squirrel/suica/MainActivity.kt` — UI
- `app/src/main/java/works/squirrel/suica/SuicaReader.kt` — NFC-F polling + balance read
- `app/src/main/AndroidManifest.xml` — `NFC` permission + tech filter

Read-only. No writes.
