# SquirrelSoft roadmap

State as of `main` at `df81f23` (2026-11-14). Android native apps (Kotlin, Android Views, CameraX/ML Kit, AudioRecord, NfcF):
**SquirrelSuica 0.0.12**, **SquirrelSuicaAnalysis 0.0.3**, **SquirrelSound 0.0.13**, **SquirrelQR 0.1.6**, all on Play closed testing.
SquirrelSound **0.0.14** (#81 and the Android parity work since) is on `main` and not yet published *(Squirrel)*.
Desktop C tools: SquirrelStation, SquirrelCAD, SquirrelGNSS, `sqdsp` + live SDL spectrum, and the new desktop readers
`squirrelsuica/desktop/` (PC/SC) and `squirrelqr/desktop/`. Shared C cores in `libs/` (`sqdsp`, `sqfelica`, `sqqr`, `sqgui`).
The SquirrelQR PWA is retired (`squirrelqr/web-demo/`, not a product). Play scripts in `scripts/`, CI in `.github/workflows/`.

Principles: original code only; each app = portable core + thin native front-ends; tests on the core; real-device checks are post-merge.
**Real Suica / nanaco card data is local test input only.** It never goes into a commit, PR, issue, comment or fixture in any form;
every committed dump is synthetic (`squirrelsuica/tests/fixtures/demo-*.json`, #75).

Every item is a checkbox. Tick it in the PR that merges its last piece. Nested boxes are PR-sized steps.

## Hand-off status (2026-11-14)

The remaining work is left for Cursor / Codex. **[HANDOFF.md](HANDOFF.md)** has a card for every open item
(files, acceptance, how to test) and the suggested order; **[docs/CONTINUANCE.md](docs/CONTINUANCE.md)** is
the plan beyond it for the whole suite (this repo, squirrelbios, squirrel-os2). In this file: `- [x]` is done
on `main`; `- [ ]` is **open for Cursor/Codex**; *(in progress)* means a branch exists, finish it rather than
redo it; *(Squirrel)* means only the owner can do it (Play Console, real cards and readers, creating repos).
The same states are labels on the GitHub issues (`status: done`, `status: in progress`, `cursor-codex`,
`needs: squirrel`). Nothing is in progress at hand-off: no open PRs, no unmerged branches.

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
- [x] `libs/sqqr` (C): original QR decoder so desktop and Squirrel OS/2 don't depend on ML Kit; now also the encoder and payload parsing.
  - [x] Step 1: decode a sampled module grid (format/version, unmasking, de-interleaving, Reed–Solomon, all data modes) (#59).
  - [x] Step 2: find a symbol in a grayscale image (binarization, finder search, alignment, homography, light-on-dark) (#63).
  - [x] Step 3: SquirrelQR on Android uses it through JNI when ML Kit finds nothing (#65).
- [x] `libs/sqmath` (C): expression engine/CAS core for the calculator suite.
  - [x] Step 1: parser (implied multiplication, factorial, error positions), evaluation, compiled programs, symbolic derivatives with simplification, printing, roots, integrals, number display; `sqcalc` CLI
  - [x] Step 2: expansion and collection with exact fractions, exact equation solving (linear, quadratic, rational roots, symbolic coefficients), equations as `left = right`; `sqcalc --expand/--collect/--solve-for`
  - [x] Step 3: factoring over the rationals, partial fractions, symbolic integrals; `sqcalc --factor/--apart/--antideriv`, SquirrelCalc `factor`, `apart`, `integrate EXPR`
  - [x] Step 4: units: dimensioned arithmetic and conversion (`5 ft to m`, `5 kg * 9.81 m/s^2` = 49.05 N, `deg to dms`); `sqcalc --units`, SquirrelCalc `units`/`convert` lines and any `... to UNIT` line
- [x] `libs/sqgui` (C): tiny immediate-mode widget layer over SDL2 for desktop, designed to port to Squirrel OS/2's wm/gfx: a 32-bit canvas core with no OS calls, Warp-grey widgets, the OS/2 5x7 font, and an SDL2 window back end.
- [x] JNI smoke tests for every Android app that uses a core (each lib already has `make test` unit tests).
  - [x] SquirrelQR: `make test` builds `sqqr_jni.c` for the host and drives it from a JVM (`squirrelqr/android/jni-smoke`)
  - [x] SquirrelSound: the same for `sqdsp_jni.c` (`squirrelsound/android/jni-smoke`)
  - [x] SquirrelSuica analysis (`sqfelica_jni.c`) (`squirrelsuica-analysis/android/jni-smoke`)

## S3 — Native front-ends, desktop parity (#30, #31)
- [x] S3.1 Retire the SquirrelQR PWA as a product: moved to `squirrelqr/web-demo/` with a "not a product" README (#66).
- [ ] S3.2 SquirrelSound desktop: SDL2 GUI with the same split view; mic and system-audio loopback on every OS.
  - [x] Step 1: capture sources: `--source mic` (SDL2 capture), `--device`, `--source loopback` from the PulseAudio/PipeWire monitor source, `squirrelsound devices`; CI with SDL's disk driver and a null sink (#78).
  - [ ] WASAPI loopback on Windows
  - [ ] macOS system audio (ScreenCaptureKit or a virtual device)
  - [x] Split-view GUI window on `libs/sqgui`: toolbar, spectrum over LOFARgram, `--gui-snapshot` for CI
- [ ] S3.3 SquirrelSuica desktop: PaSoRi RC-S380/RC-S300, same `sqfelica` parsing, GUI with balance + history.
  - [x] Step 1: `squirrelsuica/desktop/`, a C reader on `sqfelica` over a transport interface, balance and history with station names, replays SuicaAnalysis dumps (#71).
  - [x] Step 2: PC/SC transport (RC-S300 and other CCID readers; pcsc-lite, macOS PCSC, winscard), `--reader`/`--wait`/`--list-readers`, CI through pcscd and a virtual reader (#76).
  - [x] PaSoRi RC-S380 USB driver (original, libusb) — framing + InCommRF unit-tested; live path via `--usb` (#85).
  - [x] GUI window (SDL2 + SDL_ttf): balance + history with station names; `--gui` / `--gui-ms` (#86).
  - [x] CSV export (`--csv` / `--csv-file`, IDm omitted by default; `--include-idm` opt-in) (#87).
- [ ] S3.4 SquirrelQR desktop: drag a rectangle on screen → capture → `sqqr` decode; also webcam.
  - [x] Step 1: `squirrelqr/desktop/`, files (PGM/PPM/PBM, BMP) and X11 whole screen / rectangle / `--select`, CI under Xvfb (#69).
  - [x] Step 2: PNG input with its own PNG and zlib decoder, and a Wayland note (#77).
  - [ ] Wayland portal capture
  - [ ] Windows and macOS capture
  - [x] Small GUI window on `libs/sqgui` (`--gui`): select, whole screen, open, preview with outline, copy
  - [ ] Webcam
- [ ] S3.5 (PKG) Windows/macOS/Linux packaging (MSI/zip, .app, AppImage/deb) built in CI.

## S4 — CI and releases
- [x] `.github/workflows/ci.yml`: `make test`, and build plus unit tests for every Android app (debug) on ubuntu (#47).
- [x] Release workflow: a Release button that drafts a GitHub Release per app (#54); only starts the desktop jobs an app has (#64).
- [ ] Desktop builds on windows and macos runners (comes with S3.5).

## S5 — Squirrel OS/2 apps built here first
- [x] S5.1 SquirrelCalc (Desmos/TI-Nspire class: graphing, CAS, geometry, stats, spreadsheet, notebooks) on `sqmath` + `sqgui`.
  - [x] Desktop graphing window: four functions of x, variables from the calculator line, roots, d/dx, pan and zoom, `--snapshot` headless render
  - [x] Tables, integrals with shading, crossings and trace in the window
  - [x] Parametric and polar curves, sliders for variables
  - [x] Implicit curves and inequalities, lists and regression
  - [x] CAS in the calculator line: `expand`, `collect`, `d/dx`, `solve ... [for v]`
  - [x] Spreadsheet and statistics: Sheet view (A1:H50, formulas, fill, CSV), quartiles, histograms, box plots, normal/binomial/Poisson distributions, functions of ranges and lists
  - [x] Geometry: points, segments, lines, rays, circles, polygons; midpoint, intersect, perpendicular, parallel, bisector that follow their parts; dragging; measurements in any expression
  - [x] Notebooks: calculator lines and notes worked out top to bottom, results under each line, the whole session saved as a plain-text notebook file
- [x] S5.2 SquirrelPaint (MS Paint / Paintbrush class) on `sqgui`, its core freestanding.
  - [x] Window and core: pencil, brush, eraser, line, rectangle, ellipse (outlined or filled, Shift for square, circle, 45 degrees), flood fill with tolerance, colour picker, two colours and a palette, undo and redo, zoom and pan, BMP (Windows and OS/2, 1 to 32 bits) and PPM/PGM in, BMP and PPM out, drawing commands and scripts from the command line
  - [x] Selections: rectangle select, cut, copy, paste, move, nudge; flip, rotate, resize (nearest or smooth), crop, canvas size, invert
  - [x] Text (the suite's 5x7 font), spray, curves (cubic, two bends) and polygons; custom colours (typed, R/G/B sliders, into the palette)
  - [x] PNG in and out (own inflate and deflate): every colour type and depth, interlacing and transparency in; filtered and compressed RGB out
- [x] S5.3 SquirrelGames (Solitaire, FreeCell, Hearts, Bridge, Chess, Minesweeper-class, Mahjongg)
  - [x] Patience: Klondike (draw 1 or 3, scoring) and FreeCell (moves of several cards as the free cells allow), seeded deals, undo, cards going home by themselves, drag and drop, the command line
  - [x] Minesweeper-class: Mines, three sizes or any up to 40x24, the first square always clear, chords, flags and question marks, a clock, seeded fields, the command line
  - [x] Hearts: three computer players, passing, the usual first-trick and broken-hearts rules, shooting the moon, to 100
  - [x] Bridge: you South with three computer players, the auction with doubles, dummy, duplicate scoring with boards going round
  - [x] Chess: every rule and draw, standard notation and FEN, a computer player with four levels, White, Black or two people
  - [x] Mahjongg (tile matching): Tiles, three layouts of its own, deals that can always be cleared, hint, shuffle, undo
- [x] S5.4 SquirrelDebug (desktop debugger UI for Squirrel OS/2 over serial/QEMU gdbstub)
  - [x] The GDB remote protocol, an x86 disassembler (16 and 32 bits, NASM syntax), registers, memory, breakpoints, step, next, run to, continue and stop, real and protected mode told apart by QEMU's monitor, symbols from nm, a practice machine, TCP, serial and QEMU started by it, the window and the command line
- Each designed so its core compiles for Squirrel OS/2 (freestanding C, no libc beyond a small shim).

## S6 — Repo split (#32)
- [ ] *(Squirrel creates the repos; Cursor/Codex prepares the tree)* When an app has its own core lib, desktop + Android front-ends and CI: move to `squirrelsoft-suica`, `squirrelsoft-soundanalysis`, `squirrelsoft-qr`, `squirrelsoft-cad` (Station+CAD), `squirrelsoft-gnss`, `squirrelsoft-calc`, `squirrelsoft-games`, keeping history (`git filter-repo` by the human). `squirrelsoft` stays as the umbrella with shared libs and Play scripts. The human performs the actual repo creation.

## Ongoing per app
- SquirrelSuica
  - [ ] PASMO/ICOCA labels
  - [ ] Richer station tables (with source + license stated)
  - [x] Product-sale rows (terminal `CA`, #73)
  - [ ] Bus rows
  - [x] Export CSV (desktop `--csv` / `--csv-file`, #87)
- SquirrelSound
  - [x] Multi-resolution FFT (desktop: `sqdsp_multires_*`, MR / `--multires`)
  - [ ] Peak hold markers
    - [x] Desktop: labelled peaks on the live or held trace (`live_engine_peaks`)
    - [ ] Android
  - [x] Octave / 1-3 octave bars
    - [x] Shared core: `sqdsp_octave_bands` / `sqdsp_band_levels_db` (IEC 61260 base 10, ENBW-corrected); desktop Octave and 1/3 Oct views
    - [x] Android: Octave and 1/3 octave views through the JNI bridge, with band hold
  - [x] WAV record
    - [x] Desktop: Rec / `--record`, gap-free tap on the capture ring, streaming WAV writer
    - [x] Android: Rec writes 16-bit mono WAV from the capture thread, then Share
  - [x] Calibration offset per device
    - [x] Desktop: `-`/`=` and `--cal`, saved per input in `live.conf`
    - [x] Android: Cal slider (±60 dB) saved per mic source
  - [x] Desktop scope view (the oscilloscope of #28 on the desktop; Android stays under S0.4)
- SquirrelQR
  - [x] Generate QR
    - [x] Shared core: `sqqr_encode` (numeric/alphanumeric/byte, ECI, all versions and levels, penalty-scored mask; matches segno's fixtures)
    - [x] Desktop: `--encode` / `--wifi` to PNG, SVG, PGM or the terminal; Make QR and Save PNG in the window
    - [x] Android: Make QR (text or Wi-Fi network, level), Share and Save PNG
  - [x] Wi-Fi / vCard parsing
    - [x] Shared core: `sqqr_content` (Wi-Fi, vCard, MECARD, mailto/MATMSG, tel, SMS, geo, links)
    - [x] Desktop: `--details`, `"type"`/`"fields"` in `--json`, fields in the window
    - [x] Android: fields under the last read
  - [ ] Batch scan
  - [ ] Torch / zoom

## Desktop CAD/GNSS tools (carried over from the previous roadmap)

These milestones predate the suite roadmap and remain open. Formats stay owned by this project (`.sqdgn`, `.sqdwg`, `.sqobs`).

### SquirrelStation
- [x] Element graph: line, arc, polyline, curve (Catmull-Rom), shape, mesh, box/cylinder/sphere solids; length, area, volume
- [x] Levels (on/off, frozen), cells (nested placements with scale and rotation), reference attachments (moved and scaled)
- [x] Accu-style snaps (endpoint, midpoint, centre, intersection, keypoint, nearest; through cells and references) and keyed input (`xy=`, `dl=`, `di=`)
- [x] `.sqdgn` text interchange (v2, reads v1) + `.sqdgnb` binary sketch, both bounds-checked; DXF export of every element kind
- [x] Desktop window on sqgui (`squirrelstation gui`): Top/Front/Right/Iso views with zoom and pan, Line and Circle by mouse or keyin, AccuSnap, the AccuDraw compass (indexing, Ortho lock, `dl=`/`di=` from the last point), element selection with measures, undo, save
- [x] DXF import of polylines as one element, BLOCKS as cells, INSERT as placements, polyface and 3DFACE as meshes; export writes cells as BLOCKS

### SquirrelCAD
- [x] Classic command loop: LINE, CIRCLE, ARC, PLINE, LAYER, BLOCK, INSERT, plus ERASE/MOVE/COPY/ROTATE/SCALE, UNDO, LIST/DIST/AREA (`squirrelcad run`)
- [x] Model space / paper space sketch: PLOT to an SVG sheet with scale and title block
- [x] `.sqdwg` interchange (text with spaces, damaged-file checks, unique ids)
- [x] DXF-like export of *our* entities (layer colours, BLOCKS, INSERT; not Autodesk DXF fidelity claims)
- [x] Desktop window on sqgui (`squirrelcad gui`): lines, circles and polylines with the mouse, end/mid/centre and grid snaps, ortho, zoom about the pointer, pan, select and erase, the command line in the window
- [x] DXF import of BLOCKS/INSERT (nested), R12 POLYLINE, MTEXT, 3DFACE/SOLID outlines

### SquirrelGNSS
- [x] WGS84 / ECEF / geodetic conversions (exact both ways, ENU both ways, azimuth/elevation)
- [x] Broadcast-ephemeris Kepler propagator with harmonic corrections, satellite clock and relativistic term
- [x] Pseudorange least-squares PVT: elevation mask and weighting, H/V/T DOP, residuals, fault detection and exclusion
- [x] RINEX navigation and observation input (2.11 and 3.x GPS; `squirrelgnss solve OBS NAV`)
- [x] Continuous epoch stream + survey stakeout helpers (occupation averaging, speed/course, go/cut/fill guidance)
- [x] RTK double-difference math: code DGNSS, static L1 carrier float + integer fix with ratio test (`squirrelgnss rtk`)
- [x] Dual-frequency (L2) observations and wide-lane fixing; LAMBDA-style decorrelated search (`sqg_ils`)
- [x] Kinematic RTK (moving rover epoch by epoch, cycle slips, `rtk --kinematic`)
- [x] L5 observations: RINEX in and out, the synthetic sky, the ionosphere-free point position from L1/L2 or L1/L5 (`--iono-free`)
- [x] Serial receiver input (`squirrelgnss receiver`): NMEA fixes and satellites in view, u-blox RXM-RAWX/RXM-SFRBX/NAV-PVT with the navigation message decoded to ephemerides, solved live beside the receiver and logged as RINEX

### SquirrelSound desktop (sqdsp)
- [x] Radix-2 FFT, windows, periodogram, Welch PSD, STFT; pitch, THD, SNR; WAV I/O, CLI reports, SVG plots; live SDL spectrum/waterfall + ALSA mic (done)
- [x] Optimize waterfall blit (cached rows, only new ones coloured); multi-resolution FFT (original implementation)
