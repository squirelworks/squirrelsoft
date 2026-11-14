# Hand-off to Cursor / Codex (2026-11-14)

From 2026-11-14 the remaining SquirrelSoft work is left for Cursor or Codex to
pick up. This file says what is done, what is in flight, and what is open,
with enough detail to start any open item cold. What comes after this list,
for the whole suite (this repo, squirrelbios and squirrel-os2), is
[docs/CONTINUANCE.md](docs/CONTINUANCE.md).

Source of truth, in order: `ROADMAP.md` on `main`, then the GitHub issues
(#30, #31, #32, #28 and the hand-off tracker), then this file. If they
disagree, `main`'s code wins; fix the docs in your PR.

## Status legend

Used here, in `ROADMAP.md` and on the issues (as labels).

| Mark | Label | Meaning |
|------|-------|---------|
| `- [x]` | `status: done` | On `main`, covered by `make test` or the Android unit tests. |
| *(in progress)* | `status: in progress` | A branch or PR exists. Finish it; do not redo it. |
| `- [ ]` | `cursor-codex` | Open. Nobody is on it. Take it. |
| *(Squirrel)* | `needs: squirrel` | Only the owner can do it (Play Console, real cards and readers, creating repos). |

## Where things are

- **Android (Play closed testing):** SquirrelSuica 0.0.12, SquirrelSuicaAnalysis
  0.0.3, SquirrelQR 0.1.6, SquirrelSound 0.0.13 on Play; SquirrelSound **0.0.14**
  (full-screen Live tab, Settings tab, Android octave bars, WAV record,
  calibration), **0.0.15** (landscape split, scope, peak labels),
  SquirrelQR **0.1.7** (torch / zoom / batch), SquirrelSuica **0.0.13**
  (issuer labels, bus rows, WAON history), and SquirrelSuica **0.0.14**
  (richer MIT station tables) wait for Squirrel to upload.
- **Desktop (C, SDL2 via `libs/sqgui`):** SquirrelStation, SquirrelCAD,
  SquirrelGNSS, SquirrelSound (mic + PulseAudio/PipeWire loopback, GUI),
  SquirrelSuica (PC/SC, RC-S380 over libusb, GUI, CSV), SquirrelQR (files,
  PNG, X11 capture, V4L2 webcam, GUI, encoder), SquirrelCalc, SquirrelPaint, SquirrelGames,
  SquirrelDebug. Everything in S5 (the apps built here for Squirrel OS/2) is done.
- **Shared cores:** `libs/sqdsp`, `libs/sqfelica`, `libs/sqqr`, `libs/sqmath`, `libs/sqgui`.
- **Test gate:** `make test` (every lib and program, the Python SuicaAnalysis
  twin, and host JNI smokes for QR / Sound / SuicaAnalysis) and the Android job
  (`./gradlew :app:assembleDebug :app:testDebugUnitTest` per app), both in
  `.github/workflows/ci.yml` on every PR.
- **Open PRs at hand-off:** none. No in-progress branches.

## Rules (Squirrel's; do not break)

1. Base every PR on `main`. Never stack a branch on another feature branch.
   Group related steps into one sizeable PR. Before finishing, confirm the PR
   targets `main` and GitHub shows it mergeable with CI green.
2. **Real card data is local test input only.** Real Suica / PASMO / nanaco /
   WAON dumps, IDm/PMm, balances, history rows, station or terminal codes from
   Squirrel's cards never go into a commit, PR, issue, comment, fixture or
   posted log. Reproduce bugs with synthetic fixtures
   (`squirrelsuica/tests/fixtures/demo-*.json`, `libs/sqfelica/tools/gen_fixture.py`).
3. Original code only. ML Kit and CameraX on Android are the only third-party
   recognisers allowed; the desktop and Squirrel OS/2 use `sqqr`.
4. AABs, keystores, passwords and Play keys never go into the repo, PRs,
   artifacts or logs (`squirrelsoft-kit/addenda/01-RELEASE_ARTIFACTS.md` in the
   project files). Only Squirrel uploads to Play and publishes GitHub Releases.
5. Each app is a portable core + thin native front-ends; tests go on the core.
6. Never delete branches (Squirrel's call).

## Build and test

```sh
sudo apt-get install build-essential libsdl2-dev libsdl2-ttf-dev libasound2-dev \
  libx11-dev libpcsclite-dev pcscd libusb-1.0-0-dev xvfb pulseaudio python3
make            # every desktop program
make test       # every lib and program (see ci.yml for pcscd/vpcd and Xvfb set-up)
cd squirrelsound/android && ./gradlew :app:assembleDebug :app:testDebugUnitTest
```

Android apps: `squirrelsuica/android`, `squirrelsuica-analysis/android`,
`squirrelsound/android`, `squirrelqr/android` (JDK 17, Android SDK 35). Without
an SDK, the Gradle harnesses in the project's `squirrelsoft-kit/tools/` compile
and unit-test the Kotlin against stubs.

## Suggested order

1. ~~SND-landscape + SND-scope + SND-peaks~~ (done: SquirrelSound 0.0.15).
2. ~~FEL-jni-smoke~~ (done: S2 JNI box).
3. ~~QR-torch-zoom + QR-batch~~ (done: SquirrelQR 0.1.7).
4. ~~**SUICA-labels + SUICA-bus + FEL-pasmo-waon**~~ (done: SquirrelSuica 0.0.13).
5. ~~**SUICA-stations**~~ (done: SquirrelSuica 0.0.14, MIT table).
6. **PKG** (S3.5 + S4: Windows/macOS/Linux packages in CI), which unblocks
   **SND-wasapi**, **SND-macos**, **QR-capture-win-mac** and **QR-wayland**.
7. ~~**QR-webcam**~~ (done: V4L2 + synthetic CI frame).
8. **S6 repo split** with Squirrel.

## Task cards

### SND-landscape — side-by-side layout in landscape  `status: done` (#28)
- Done in 0.0.15: landscape spectrum|LOFARgram, same split ratios, one row per
  pixel, `configChanges` so rotate does not restart capture; `SplitLayoutTest`.

### SND-scope — optional oscilloscope in the top half on Android  `status: done` (#28)
- Done in 0.0.15: Settings scope switch, rising-edge trigger + auto timebase;
  `ScopeTriggerTest` on a synthetic sine.

### SND-peaks — peak hold markers on Android  `status: done`
- Done in 0.0.15: `sqdsp_local_peaks` via `SqDsp.peaks` / `nativePeaks`; labelled
  peaks on the live or held trace; `jni-smoke` calls the new JNI entry.

### SND-wasapi / SND-macos — system audio on Windows and macOS  `cursor-codex`
- **Files:** `squirrelsound/src/live_audio.c` (add `live_audio_wasapi.c`,
  `live_audio_coreaudio.m` behind the same interface), Makefile.
- **Acceptance:** `squirrelsound devices` lists the loopback source;
  `--source loopback` captures it; CI on `windows-latest` / `macos-latest`
  builds it and runs the capture test against a silent device (needs PKG).

### FEL-jni-smoke — JVM smoke test for `sqfelica_jni.c`  `status: done`
- On `main` via #133 / #136 (S2 JNI box). Host build of `sqfelica_jni.c`
  driven from `android/jni-smoke`, hooked into `make test` (synthetic parity
  blocks only).

### FEL-pasmo-waon — PASMO / WAON history in C  `status: done` (with SUICA-labels/bus)
- WAON 0x680b 32-byte history in Python, C (`sqf_parse_waon_*`) and the JNI
  bridge; PASMO uses the same Suica history layout (issuer label distinguishes it).

### SUICA-labels / SUICA-bus  `status: done` (SquirrelSuica 0.0.13)
- Issuer labels from attributes `0x008B` byte 8 (+ history settlement) in
  Python, C and Kotlin; bus rows show operator/stop codes and never hit the
  station table. **SUICA-stations** (richer MIT tables) done in 0.0.14.

### SUICA-stations  `status: done` (SquirrelSuica 0.0.14)
- Richer station tables from soltia48/suica-viewer `station_codes.csv` (MIT),
  stated in `squirrelsuica/data/README`. Same schema + optional `note` in
  Python, C (`sqfelica`) and Kotlin; SFCardFan dump removed (no redistribution).

### QR-torch-zoom / QR-batch — SquirrelQR on Android  `status: done` (0.1.7)
- **Files:** `squirrelqr/android/app/src/main/java/works/squirrel/qr/MainActivity.kt`,
  `ScanHistoryStore.kt` (`BatchScanSession`), unit tests in `BatchScanSessionTest.kt`.
- **Acceptance:** a torch toggle and pinch/slider zoom through CameraX
  `CameraControl`; batch mode keeps scanning, de-duplicates by content and
  exports the list (share as text/CSV). Unit tests on the de-duplication.

### QR-webcam — desktop camera capture  `status: done`
- `--webcam` / `squirrelqr devices` on V4L2 (`webcam_v4l2.c`); Win/mac link
  `webcam_stub.c` (`mf-stub` / `avf-stub`) until PKG. CI: `test_webcam` and
  `webcam_smoke.sh` decode a synthetic frame via `--device @FILE` or
  `QR_WEBCAM_FRAME` (no real camera).

### QR-wayland / QR-capture-win-mac — screen capture  `cursor-codex`
- **Files:** `squirrelqr/desktop/src/capture_*.c` (add `capture_portal.c` via the
  xdg-desktop-portal Screenshot API over D-Bus, `capture_win.c` with GDI/DXGI,
  `capture_mac.m` with ScreenCaptureKit).
- **Acceptance:** `--select` works on GNOME Wayland; Windows and macOS builds
  capture a region. Needs PKG for Win/mac CI runners.

### PKG — Windows/macOS/Linux packages in CI (S3.5, S4)  `cursor-codex`
- **Acceptance:** `.github/workflows/release.yml` builds a zip (or MSI) for
  Windows, a `.app` in a `.dmg` for macOS and an AppImage plus `.deb` for
  Linux for every desktop program, runs `make test` on all three runners and
  attaches the packages to the drafted Release. Unsigned is fine to start.

### PLAY-0.0.14 — upload SquirrelSound 0.0.14  `needs: squirrel`
- Build the AAB locally with the upload key and upload it to closed testing
  (`scripts/` has the Play helpers). Do not commit or attach the AAB anywhere.

### S6 — per-app repos  `needs: squirrel` (prep `cursor-codex`)
- Squirrel creates the repos and runs `git filter-repo`. Cursor/Codex can
  prepare: a script that lists the paths per app, CI files per future repo,
  and the umbrella's README links.

## Issues to close or keep

- #24, #27, #29 (implementation done), #85, #86, #87 (S3.3 steps merged), and
  the probes #83, #84 are closed with this hand-off.
- #28 can close once 0.0.15 lands (landscape + scope done; peaks too). #30, #31,
  #32 (epics) and #88 (forward plan) stay open.
