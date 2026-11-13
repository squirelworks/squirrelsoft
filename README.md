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
| `squirrelsuica/` | **SquirrelSuica** | FeliCa Suica/PASMO reader with station-name history (USB + Android) |
| `squirrelcalc/` | **SquirrelCalc** | Graphing calculator on `libs/sqmath` + `libs/sqgui` (desktop) |

## Build

Requires a C11 toolchain (`gcc` or `clang`) and `make`.
Live spectrum UI optionally uses SDL2 + ALSA (`pkg-config sdl2 alsa`).

```bash
make            # build C programs
make test       # C + QR/Suica smoke tests
make clean
```

SquirrelQR is a native app (`squirrelqr/android/`); `squirrelqr/web-demo/` is an
unsupported browser demo (`make -C squirrelqr serve`).

Suica demo (no card reader required):

```bash
python3 squirrelsuica/suica_read.py --demo
```

## CI

`.github/workflows/ci.yml` runs on every push to `main` and every pull request:

- `make test` on Ubuntu, covering the C tools (with SDL2 and ALSA) and the Python smoke tests.
- `./gradlew :app:assembleDebug :app:testDebugUnitTest` for each Android app, each in its own job.

It builds debug only. It needs no signing secrets and never builds or uploads release bundles (`.aab`).

`.github/workflows/release.yml` is the release button: run it from the Actions tab for one app, and it builds the signed APK and the desktop builds into a **draft** Release. Squirrel publishes the draft, which creates the tag. See [docs/RELEASING.md](docs/RELEASING.md).

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
├── squirrelqr/        # QR code reader (Android; desktop planned; web-demo/ unsupported)
├── squirrelsuica/     # FeliCa reader only (nfcpy + Android NFC-F)
└── squirrelcalc/      # graphing calculator window (sqmath + sqgui)
```

## License

Squirrelsoft is free software under the GNU General Public License,
version 3 ([LICENSE](LICENSE)). The Gradle wrapper scripts and jars in the
Android projects (`gradlew`, `gradlew.bat`, `gradle/wrapper/`) are Gradle's
own files under the Apache License 2.0, as their headers say.

## License posture

Original Squirrelsoft source. Do not drop proprietary MicroStation, AutoCAD,
vendor GNSS firmware, or Numerical Recipes book code into this tree.
SquirrelSound implements standard spectral methods from first principles.
Suica support is **read-only** balance/history via published FeliCa services.
