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
| `squirrelsound/` | **SquirrelSound** | Spectral sound analyzer (`sqdsp`) + live UI |
| `squirrelfield/` | **SquirrelField** | Phone PWA: live spectrum, QR reader, Suica UI |
| `squirrelsuica/` | **SquirrelSuica** | FeliCa Suica/PASMO reader (USB + Android) |

## Build

Requires a C11 toolchain (`gcc` or `clang`) and `make`.
Live spectrum UI optionally uses SDL2 + ALSA (`pkg-config sdl2 alsa`).

```bash
make            # build C programs
make test       # C + field/Suica smoke tests
make clean
```

Phone field UI:

```bash
make -C squirrelfield serve   # http://0.0.0.0:8765/
```

Suica demo (no card reader required):

```bash
python3 squirrelsuica/suica_read.py --demo
```

## Layout

```
squirrelsoft/
├── README.md
├── ROADMAP.md
├── Makefile
├── squirrelstation/
├── squirrelcad/
├── squirrelgnss/
├── squirrelsound/     # FFT analyzer + live mic/demo SDL waterfall
├── squirrelfield/     # phone PWA (spectrum / QR / Suica)
└── squirrelsuica/     # FeliCa reader (nfcpy + Android NFC-F)
```

## License posture

Original Squirrelsoft source. Do not drop proprietary MicroStation, AutoCAD,
vendor GNSS firmware, or Numerical Recipes book code into this tree.
SquirrelSound implements standard spectral methods from first principles.
Suica support is **read-only** balance/history via published FeliCa services.
