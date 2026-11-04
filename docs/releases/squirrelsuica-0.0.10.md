SquirrelSuica / Suica Reader 0.0.10 (versionCode 10), the first release since 0.0.8.

## What's new

- **Full history reads.** Fixed the block-list encoding in Read Without Encryption, so history blocks past the first few come back correctly on cards that rejected the old requests.
- **More reliable taps.** Cards are read in NFC reader mode on a worker thread. The screen stays responsive, and the app skips the NDEF probe that could lose the card before the reads finished.
- **Desktop reader.** `suica_read.py` reads in chunks, splits nfcpy's results into 16-byte blocks, and drops to one block at a time when the card refuses a chunk.

## Downloads

- `SquirrelSuica-0.0.10-android.apk`: signed Android build (Android 8.0+, NFC).
- `SquirrelSuica-0.0.10-python.zip`: the desktop reader and parser (Python 3.9+ with nfcpy and a Sony PaSoRi reader; Linux, Windows or macOS). See `squirrelsuica/README.md` inside.
- `SHA256SUMS.txt`.

Play users get the same version through the store.
