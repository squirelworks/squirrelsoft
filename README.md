# squirrelsoft

Application suite for Squirrel-OS/2 and host development.

All code here is project-native. Product names below are original Squirrelsoft
titles for clean-room reimplementations inspired by well-known CAD, GNSS, and
audio-analysis workflows — not ports of proprietary binaries or copyrighted
libraries.

| Directory | Program | Inspired domain |
|-----------|---------|-----------------|
| `squirrelstation/` | **SquirrelStation** | MicroStation-class 3D design / DGN-style CAD |
| `squirrelcad/` | **SquirrelCAD** | AutoCAD-class 2D/3D drafting |
| `squirrelgnss/` | **SquirrelGNSS** | Continuous satellite / GPS land surveying |
| `squirrelsound/` | **SquirrelSound** | Spectral sound analyzer (`sqdsp`) + phone Spectroid UI |
| `squirrelqr/` | **SquirrelQR** | Phone QR code reader only |
| `squirrelsuica/` | **Suica Reader (SquirrelSuica)** | FeliCa Suica/PASMO reader with station-name history (USB + Android) |

## Build

Requires a C11 toolchain (`gcc` or `clang`) and `make`.
Live spectrum UI optionally uses SDL2 + ALSA (`pkg-config sdl2 alsa`).

```bash
make            # build C programs
make test       # C + QR/Suica smoke tests
make clean
```

QR code reader PWA:

```bash
make -C squirrelqr serve   # http://0.0.0.0:8765/
```

Suica demo (no card reader required):

```bash
python3 squirrelsuica/suica_read.py --demo
```

## CI

`.github/workflows/ci.yml` runs on every push to `main` and every pull request:

- `make test` on Ubuntu, covering the C tools (with SDL2 and ALSA) and the Python smoke tests.
- `./gradlew :app:assembleDebug :app:testDebugUnitTest` for each Android app, each in its own job.

It builds debug only. It needs no signing secrets and never builds or uploads release bundles (`.aab`).

## Layout

```
squirrelsoft/
├── README.md
├── ROADMAP.md
├── Makefile
├── squirrelstation/
├── squirrelcad/
├── squirrelgnss/
├── squirrelsound/     # FFT analyzer + live mic Spectroid UI (desktop + Android)
├── squirrelqr/        # phone QR code reader (PWA + Android)
└── squirrelsuica/     # FeliCa reader only (nfcpy + Android NFC-F)
```

## License posture

Original Squirrelsoft source. Do not drop proprietary MicroStation, AutoCAD,
vendor GNSS firmware, or Numerical Recipes book code into this tree.
SquirrelSound implements standard spectral methods from first principles.
Suica support is **read-only** balance/history via published FeliCa services.
