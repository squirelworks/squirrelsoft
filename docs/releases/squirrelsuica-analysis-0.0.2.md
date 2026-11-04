SquirrelSuicaAnalysis 0.0.2 (versionCode 2).

A FeliCa debug and analysis app for looking at what a card actually answers, to help improve SquirrelSuica's parsers.

## What's new

- **Its own launcher icon**, so it no longer looks like Suica Reader on the home screen.
- **Play Store listing** with graphics and screenshots, ahead of closed testing.

The scanning and export features are the same as 0.0.1.

## What it does

- Polls every system code on the card, enumerates services and areas (Search Service Code, Request Service), and decodes PMm into the card's maximum response times.
- Reads every key-free service with chunk sizes 1 to 15 and records timings and status flags.
- Exports a JSON dump through the share sheet or saves it with the file picker, with an option to redact the IDm.
- `suica_analyze.py` is the desktop twin and writes the same JSON format.

## Downloads

- `SquirrelSuicaAnalysis-0.0.2-android.apk`: signed Android build (Android 8.0+, NFC).
- `SquirrelSuicaAnalysis-0.0.2-python.zip`: the desktop twin (Python 3.9+ with nfcpy and a Sony PaSoRi reader).
- `SHA256SUMS.txt`.

Dumps can include card history. Use the redact option before sharing one.
