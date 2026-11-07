# SquirrelSoft roadmap

State as of `main` at `0449284` (2026-11-05). Android native apps (Kotlin, Android Views, CameraX/ML Kit, AudioRecord, NfcF):
**SquirrelSuica 0.0.12**, **SquirrelSuicaAnalysis 0.0.3**, **SquirrelSound 0.0.13**, **SquirrelQR 0.1.6**, all on Play closed testing.
SquirrelSound **0.0.14** (#81) is on `main` and not yet published.
Desktop C tools: SquirrelStation, SquirrelCAD, SquirrelGNSS, `sqdsp` + live SDL spectrum, and the new desktop readers
`squirrelsuica/desktop/` (PC/SC) and `squirrelqr/desktop/`. Shared C cores in `libs/` (`sqdsp`, `sqfelica`, `sqqr`).
The SquirrelQR PWA is retired (`squirrelqr/web-demo/`, not a product). Play scripts in `scripts/`, CI in `.github/workflows/`.

Principles: original code only; each app = portable core + thin native front-ends; tests on the core; real-device checks are post-merge.
**Real Suica / nanaco card data is local test input only.** It never goes into a commit, PR, issue, comment or fixture in any form;
every committed dump is synthetic (`squirrelsuica/tests/fixtures/demo-*.json`, #75).

Every item is a checkbox. Tick it in the PR that merges its last piece. Nested boxes are PR-sized steps.

## S0 — Fix what users hit today (0.0.9 / 0.1.6)
- [x] S0.1 Suica history block-list encoding fix + reader mode on a background thread (#33, #36, #38; issue 02).
- [x] S0.2 SquirrelSound LOFARgram span actually sets the scroll rate (#35, closes #29).
- [x] S0.3 SquirrelSound levels and colour: true dBFS, black-floor colour map, −120…−20 dB with Auto, mic source picker and gain (#43, #44, #45; #27).
- [x] S0.4 SquirrelSound split layout: 50/50 default, 40/60 and 60/40, one waterfall row per pixel, time axis (#46; #28). The Live tab gives the plot the whole screen and the controls move to a Settings tab (#81, 0.0.14).
  - [ ] Landscape side-by-side layout (#28)
  - [ ] Optional oscilloscope view in the top half (#28)
- [x] S0.5 SquirrelQR overlay mapping + fixed framing guide default, tracking optional (#37).
- [x] Waterfall / Fountain direction option, Android and desktop (#42).

## S1 — SquirrelSuicaAnalysis (new app, `works.squirrel.suicaanalysis`, #24)
- [x] S1.1 Raw explorer: system codes, service enumeration, Request Response, PMm decode, per-service block reads with chunk sizes 1..15, timings, status flags (#39); only polls system codes the card advertises (#61).
- [x] S1.2 Export JSON/hex dump, share intent, Save JSON, redact IDm option; desktop twin `suica_analyze.py` with the same JSON (#39, #53).
- [x] History tab through `libs/sqfelica` (JNI) (#58); desktop `suica_analyze.py --history`, also from a phone dump with `-i` (#60).
- [x] S1.3 Findings fed back into SquirrelSuica: shop sales on terminal type `CA` carry a time + terminal ID, not stations, in Python, C and Kotlin (#73); nanaco read by polling only advertised system codes (#62).
- [x] Made-up debugging dumps (Suica and nanaco) as fixtures and demo input (#75).
- [x] Play: privacy policy (#52), store graphics and launcher icon (#55), 0.0.2 (#56) and 0.0.3 (#68); 0.0.3 is on closed testing.

## S2 — Shared-core architecture (#32)
- [x] `docs/architecture.md`; `sqdsp` moved to `libs/sqdsp` with its own tests and a Kotlin/C FFT parity fixture (#48).
- [x] `libs/sqfelica` (C): FeliCa command builders and response parsers, Suica/nanaco/Edy history and balances, parity fixture from `felica_parse.py` (#49).
  - [x] Station table loader (`sqf_station_index` / `sqf_station_resolve` over `squirrelsuica/data/StationCode.csv`), checked against `resolve_rail` on 806 codes (#70).
  - [ ] PASMO / WAON history parsing in C
- [x] `libs/sqdsp` is the SquirrelSound core: Android spectrum frames go through it via JNI, `Fft.kt` as fallback (#67).
- [x] `libs/sqqr` (C): original QR decoder so desktop and Squirrel OS/2 don't depend on ML Kit.
  - [x] Step 1: decode a sampled module grid (format/version, unmasking, de-interleaving, Reed–Solomon, all data modes) (#59).
  - [x] Step 2: find a symbol in a grayscale image (binarization, finder search, alignment, homography, light-on-dark) (#63).
  - [x] Step 3: SquirrelQR on Android uses it through JNI when ML Kit finds nothing (#65).
- [ ] `libs/sqmath` (C): expression engine/CAS core for the calculator suite.
- [ ] `libs/sqgui` (C): tiny immediate-mode widget layer over SDL2 for desktop, designed to port to Squirrel OS/2's wm/gfx.
- [ ] JNI smoke tests for every Android app that uses a core (each lib already has `make test` unit tests).

## S3 — Native front-ends, desktop parity (#30, #31)
- [x] S3.1 Retire the SquirrelQR PWA as a product: moved to `squirrelqr/web-demo/` with a "not a product" README (#66).
- [ ] S3.2 SquirrelSound desktop: SDL2 GUI with the same split view; mic and system-audio loopback on every OS.
  - [x] Step 1: capture sources: `--source mic` (SDL2 capture), `--device`, `--source loopback` from the PulseAudio/PipeWire monitor source, `squirrelsound devices`; CI with SDL's disk driver and a null sink (#78).
  - [ ] WASAPI loopback on Windows
  - [ ] macOS system audio (ScreenCaptureKit or a virtual device)
  - [ ] Split-view GUI window
- [ ] S3.3 SquirrelSuica desktop: PaSoRi RC-S380/RC-S300, same `sqfelica` parsing, GUI with balance + history.
  - [x] Step 1: `squirrelsuica/desktop/`, a C reader on `sqfelica` over a transport interface, balance and history with station names, replays SuicaAnalysis dumps (#71).
  - [x] Step 2: PC/SC transport (RC-S300 and other CCID readers; pcsc-lite, macOS PCSC, winscard), `--reader`/`--wait`/`--list-readers`, CI through pcscd and a virtual reader (#76).
  - [x] PaSoRi RC-S380 USB driver (original, libusb) — framing + InCommRF unit-tested; live path via `--usb` (#85).
  - [x] GUI window (SDL2 + SDL_ttf): balance + history with station names; `--gui` / `--gui-ms` (#86).
  - [ ] CSV export
- [ ] S3.4 SquirrelQR desktop: drag a rectangle on screen → capture → `sqqr` decode; also webcam.
  - [x] Step 1: `squirrelqr/desktop/`, files (PGM/PPM/PBM, BMP) and X11 whole screen / rectangle / `--select`, CI under Xvfb (#69).
  - [x] Step 2: PNG input with its own PNG and zlib decoder, and a Wayland note (#77).
  - [ ] Wayland portal capture
  - [ ] Windows and macOS capture
  - [ ] Small GUI window
  - [ ] Webcam
- [ ] S3.5 Windows/macOS/Linux packaging (MSI/zip, .app, AppImage/deb) built in CI.

## S4 — CI and releases
- [x] `.github/workflows/ci.yml`: `make test`, and build plus unit tests for every Android app (debug) on ubuntu (#47).
- [x] Release workflow: a Release button that drafts a GitHub Release per app (#54); only starts the desktop jobs an app has (#64).
- [ ] Desktop builds on windows and macos runners (comes with S3.5).

## S5 — Squirrel OS/2 apps built here first
- [ ] S5.1 SquirrelCalc (Desmos/TI-Nspire class: graphing, CAS, geometry, stats, spreadsheet, notebooks) on `sqmath` + `sqgui`.
- [ ] S5.2 SquirrelPaint
- [ ] S5.3 SquirrelGames (Solitaire, FreeCell, Hearts, Bridge, Chess, Minesweeper-class, Mahjongg)
- [ ] S5.4 SquirrelDebug (desktop debugger UI for Squirrel OS/2 over serial/QEMU gdbstub)
- Each designed so its core compiles for Squirrel OS/2 (freestanding C, no libc beyond a small shim).

## S6 — Repo split (#32)
- [ ] When an app has its own core lib, desktop + Android front-ends and CI: move to `squirrelsoft-suica`, `squirrelsoft-soundanalysis`, `squirrelsoft-qr`, `squirrelsoft-cad` (Station+CAD), `squirrelsoft-gnss`, `squirrelsoft-calc`, `squirrelsoft-games`, keeping history (`git filter-repo` by the human). `squirrelsoft` stays as the umbrella with shared libs and Play scripts. The human performs the actual repo creation.

## Ongoing per app
- SquirrelSuica
  - [ ] PASMO/ICOCA labels
  - [ ] Richer station tables (with source + license stated)
  - [x] Product-sale rows (terminal `CA`, #73)
  - [ ] Bus rows
  - [ ] Export CSV
- SquirrelSound
  - [ ] Multi-resolution FFT
  - [ ] Peak hold markers
  - [ ] Octave / 1-3 octave bars
  - [ ] WAV record
  - [ ] Calibration offset per device
- SquirrelQR
  - [ ] Generate QR
  - [ ] Wi-Fi / vCard parsing
  - [ ] Batch scan
  - [ ] Torch / zoom

## Desktop CAD/GNSS tools (carried over from the previous roadmap)

These milestones predate the suite roadmap and remain open. Formats stay owned by this project (`.sqdgn`, `.sqdwg`, `.sqobs`).

### SquirrelStation
- [ ] Element graph: line, arc, curve, mesh, solid placeholders
- [ ] Levels, cells, reference attachments
- [ ] Accu-style snaps and precise keyed input
- [ ] `.sqdgn` text interchange + binary sketch

### SquirrelCAD
- [ ] Classic command loop: LINE, CIRCLE, ARC, PLINE, LAYER, BLOCK, INSERT
- [ ] Model space / paper space sketch
- [ ] `.sqdwg` interchange
- [ ] DXF-like export of *our* entities (not Autodesk DXF fidelity claims)

### SquirrelGNSS
- [ ] WGS84 / ECEF / geodetic conversions
- [ ] Broadcast-ephemeris style Kepler propagator (simplified)
- [ ] Pseudorange least-squares PVT
- [ ] Continuous epoch stream + survey stakeout helpers
- [ ] Dual-frequency / RTK double-difference hooks (math first)

### SquirrelSound desktop (sqdsp)
- [x] Radix-2 FFT, windows, periodogram, Welch PSD, STFT; pitch, THD, SNR; WAV I/O, CLI reports, SVG plots; live SDL spectrum/waterfall + ALSA mic (done)
- [ ] Optimize waterfall blit; multi-resolution FFT (original implementation)
