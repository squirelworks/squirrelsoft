# SquirrelField

Phone-first field toolkit for Squirrelsoft: live spectrum (Spectroid-class),
QR camera reader, and Suica / FeliCa transit-card reader entry points.

## Phone web app

Static PWA under `web/` — open on your phone (HTTPS or localhost):

```bash
make serve   # http://0.0.0.0:8765/
```

| Page | Purpose |
|------|---------|
| `web/index.html` | Hub |
| `web/spectrum.html` | Live mic spectrum + waterfall (pinch zoom, max-hold) |
| `web/qr.html` | Camera QR / barcode reader |
| `web/suica.html` | Suica reader UI + link to native/nfcpy path |

Installable via browser “Add to Home Screen” (`manifest.webmanifest`).

### Mic / camera permissions

Use **HTTPS** or **localhost**. Chrome on Android is the primary target for
`getUserMedia`, `BarcodeDetector`, and (limited) Web NFC.

## Suica on phone vs USB

FeliCa (Suica/PASMO) needs **NFC-F**. Browser Web NFC on Chrome is mostly
NFC-A/B and usually cannot read Suica balances. For real Suica reads:

- **USB PaSoRi (RC-S380 etc.)** on a computer → `../squirrelsuica/`
- **Android NFC-F** → `../squirrelsuica/android/` companion app sources

`web/suica.html` still provides the phone UX, demo mode, and deep-link hooks.
