SquirrelSuicaAnalysis 0.0.1 (versionCode 1), the first release of this tool.

A FeliCa debug and analysis app for looking at what a card actually answers, to help improve SquirrelSuica's parsers.

## What it does

- Polls every system code on the card, enumerates services and areas (Search Service Code, Request Service), and decodes PMm into the card's maximum response times.
- Reads every key-free service with chunk sizes 1 to 15 and records timings and status flags.
- Shows the card's history and balance (Suica/PASMO, nanaco, Edy, WAON) parsed by the shared SquirrelSoft FeliCa core, next to the raw blocks they came from.
- Exports a JSON dump through the share sheet or saves it with the file picker, with an option to redact the IDm.
- `suica_analyze.py` is the desktop twin and writes the same JSON format.

## Downloads

- `SquirrelSuicaAnalysis-0.0.1-android.apk`: signed Android build (Android 8.0+, NFC).
- `SquirrelSuicaAnalysis-0.0.1-python.zip`: the desktop twin (Python 3.9+ with nfcpy and a Sony PaSoRi reader).
- `SHA256SUMS.txt`.

Dumps can include card history. Use the redact option before sharing one.
