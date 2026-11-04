# SquirrelSoft roadmap

Continues from current `main` (`7cc37ba`): Android native apps **SquirrelSuica 0.0.8**, **SquirrelSound 0.0.8**, **SquirrelQR 0.1.5**
(Kotlin, Android Views, CameraX/ML Kit, AudioRecord, NfcF); desktop C tools SquirrelStation, SquirrelCAD, SquirrelGNSS,
`sqdsp` + live SDL/ALSA spectrum; Python `suica_read.py` (nfcpy); SquirrelQR PWA in `squirrelqr/web/`; Play scripts in `scripts/`.

Principles: original code only; each app = portable core + thin native front-ends; tests on the core; real-device checks are post-merge.

## S0 — Fix what users hit today (0.0.9 / 0.1.6)
- S0.1 Suica history block-list encoding fix + reader mode on a background thread (issue 02). **Landed** in #33, #36, #38. Test: unit tests on command bytes for blocks 0..19; fake-NfcF test reading 20 blocks.
- S0.2 SquirrelSound LOFARgram rate actually follows the setting (issue 06). **In review:** #35. Test: engine-level test feeding N seconds of samples produces sampleRate/hop rows ±1.
- S0.3 SquirrelSound levels and colour (issue 04). Test: full-scale sine reads 0 dBFS ±0.5; colour map unit test.
- S0.4 SquirrelSound 50/50 split layout (issue 05).
- S0.5 SquirrelQR overlay mapping + fixed framing guide default, tracking optional (issue 03). **Landed** in #37. Test: PreviewMapper rotation cases.

## S1 — SquirrelSuicaAnalysis (new app, `works.squirrel.suicaanalysis`) (issue 01)
- S1.1 Raw explorer: polling all system codes (Request System Code), Request Service/Search Service Code enumeration, Request Response, PMm decode (max response time), per-service block reads with chunk sizes 1..15, timings, status flags.
- S1.2 Export JSON/hex dump, share intent, redact IDm option. Python desktop twin `suica_analyze.py` producing the same JSON. S1.1 and S1.2 **in review:** #39.
- Parsed balances and history: Android History tab through `libs/sqfelica` (JNI); desktop `suica_analyze.py --history`, which also reads a dump exported from the phone (`-i dump.json`).
- S1.3 Findings fed back into SquirrelSuica parsers (synthetic fixture tests for what exported dumps show; the dumps themselves stay local).
  - **In review:** checks on real cards (SquirrelSuicaAnalysis 0.0.3) showed shop sales on terminal type `CA` (process `46`) decoded as station codes; Python, C (`sqfelica`) and Kotlin now treat any non-bus sale record as time + terminal ID. Real card data never goes into the repo in any form: dumps are local test input only, and every committed fixture is synthetic.

## S2 — Shared-core architecture (issue 09)
- **Done:** `docs/architecture.md`; `sqdsp` moved to `libs/sqdsp` with its own tests and a Kotlin/C FFT parity fixture.
- **Done:** `libs/sqfelica`: FeliCa command builders and response parsers, Suica/nanaco/Edy history and balances in C, with a parity fixture generated from `felica_parse.py`. **In review:** the station table loader (`sqf_station_index` / `sqf_station_resolve` over a caller buffer holding `squirrelsuica/data/StationCode.csv`), checked against `resolve_rail` on 806 codes.
- `libs/sqdsp` (exists) becomes the SquirrelSound core used by Android via JNI/NDK and by desktop. **In review:** Android spectrum frames go through `libs/sqdsp` (JNI), with `Fft.kt` as fallback.
- `libs/sqfelica` (C): FeliCa command builder/parser, Suica/PASMO/nanaco/Edy/WAON history parsing, station DB loader.
- `libs/sqqr` (C): original QR decoder (finder patterns, perspective, Reed–Solomon) so desktop and Squirrel OS/2 don't depend on ML Kit.
  - **Done:** step 1, decoding a sampled module grid: format/version information, unmasking, block de-interleaving, Reed–Solomon error correction and all data modes (numeric, alphanumeric, byte, Kanji, ECI), tested against symbols from an independent encoder.
  - **Done:** step 2, finding a symbol in a grayscale image: local-threshold binarization, 1:1:3:1:1 finder search, version information read before sampling, alignment patterns found outwards from the top-left with a least-squares homography plus local residual correction, and light-on-dark symbols. Needs about 3 pixels per module. `build/sqqr_decode photo.pgm` decodes a file.
  - **Done:** step 3, SquirrelQR on Android uses it through JNI on frames where ML Kit finds nothing; the decoder reports the symbol's corners for the overlay.
- `libs/sqmath` (C): expression engine/CAS core for the calculator suite.
- `libs/sqgui` (C): tiny immediate-mode widget layer over SDL2 for desktop, designed to port to Squirrel OS/2's wm/gfx.
- Test: each lib has `make test` unit tests; Android apps get JNI smoke tests.

## S3 — Native front-ends, desktop parity (issues 07, 08)
- S3.1 Retire the SquirrelQR PWA as a product (move to `squirrelqr/web-demo/` or delete); docs say "native apps". **Done:** moved to `squirrelqr/web-demo/` with a "not a product" README; docs and the SquirrelSound help text no longer point at it.
- S3.2 SquirrelSound desktop: SDL2 GUI with the same split view; sources: mic (ALSA/PulseAudio/PipeWire, WASAPI, CoreAudio) **and system audio loopback** (PipeWire/Pulse monitor source, WASAPI loopback, macOS via ScreenCaptureKit audio or a virtual device).
- S3.3 SquirrelSuica desktop: PaSoRi RC-S380/RC-S300 over libusb (original driver) or PC/SC on Windows; same `sqfelica` parsing; GUI with balance + history.
  - **In review:** step 1, `squirrelsuica/desktop/`: a C reader on `sqfelica` that runs the card-reading steps (transit, then the nanaco/Edy/WAON purses, chunked reads with a one-block fallback) over a transport interface, shows balance and history with station names, and replays SquirrelSuicaAnalysis dumps through a simulated card. Next: the PC/SC transport (RC-S300 and other CCID readers), then the PaSoRi USB driver and a GUI window.
- S3.4 SquirrelQR desktop: drag a rectangle on screen → capture (X11/XShm, Wayland portal, Windows Graphics Capture/GDI, macOS ScreenCaptureKit) → `sqqr` decode; also webcam.
  - **In review:** step 1, `squirrelqr/desktop/`: a command-line reader on `sqqr` that decodes image files (PGM/PPM/PBM, BMP) and, on X11, the whole screen, a fixed rectangle or one dragged with the mouse (`--select`); text uses the Android app's charset rules; tested under Xvfb in CI. Next: PNG input, the Wayland portal, Windows and macOS capture, a small GUI window, webcam.
- S3.5 Windows/macOS/Linux packaging (MSI/zip, .app, AppImage/deb) built in CI.

## S4 — CI
- **In review:** `.github/workflows/ci.yml` runs `make test` and builds plus unit-tests every Android app (debug) on ubuntu. Windows and macOS desktop builds come with S3.5 packaging.
- GitHub Actions: `make test`, Android `assembleDebug testDebugUnitTest` per app, desktop builds on ubuntu/windows/macos.

## S5 — Squirrel OS/2 apps built here first
- S5.1 SquirrelCalc (Desmos/TI-Nspire class: graphing, CAS, geometry, stats, spreadsheet, notebooks) on `sqmath` + `sqgui`.
- S5.2 SquirrelPaint, S5.3 SquirrelGames (Solitaire, FreeCell, Hearts, Bridge, Chess, Minesweeper-class, Mahjongg), S5.4 SquirrelDebug (desktop debugger UI for Squirrel OS/2 over serial/QEMU gdbstub).
- Each designed so its core compiles for Squirrel OS/2 (freestanding C, no libc beyond a small shim).

## S6 — Repo split (issue 09)
- When an app has its own core lib, desktop + Android front-ends and CI: move to `squirrelsoft-suica`, `squirrelsoft-soundanalysis`, `squirrelsoft-qr`, `squirrelsoft-cad` (Station+CAD), `squirrelsoft-gnss`, `squirrelsoft-calc`, `squirrelsoft-games`, keeping history (`git filter-repo` by the human). `squirrelsoft` stays as the umbrella with shared libs and Play scripts. The human performs the actual repo creation.

## Ongoing per app
- SquirrelSuica: PASMO/ICOCA labels, richer station tables (with source + license stated), product-sale/bus rows, export CSV.
- SquirrelSound: multi-resolution FFT, peak hold markers, octave/1-3 octave bars, WAV record, calibration offset per device.
- SquirrelQR: generate QR, Wi-Fi/vCard parsing, batch scan, torch/zoom.

## Desktop CAD/GNSS tools (carried over from the previous roadmap)

These milestones predate the suite roadmap and remain open. Formats stay owned by this project (`.sqdgn`, `.sqdwg`, `.sqobs`).

### SquirrelStation
- Element graph: line, arc, curve, mesh, solid placeholders
- Levels, cells, reference attachments
- Accu-style snaps and precise keyed input
- `.sqdgn` text interchange + binary sketch

### SquirrelCAD
- Classic command loop: LINE, CIRCLE, ARC, PLINE, LAYER, BLOCK, INSERT
- Model space / paper space sketch
- `.sqdwg` interchange
- DXF-like export of *our* entities (not Autodesk DXF fidelity claims)

### SquirrelGNSS
- WGS84 / ECEF / geodetic conversions
- Broadcast-ephemeris style Kepler propagator (simplified)
- Pseudorange least-squares PVT
- Continuous epoch stream + survey stakeout helpers
- Dual-frequency / RTK double-difference hooks (math first)

### SquirrelSound desktop (sqdsp)
- Radix-2 FFT, windows, periodogram, Welch PSD, STFT; pitch, THD, SNR; WAV I/O, CLI reports, SVG plots; live SDL spectrum/waterfall + ALSA mic (done)
- Optimize waterfall blit; multi-resolution FFT (original implementation)
