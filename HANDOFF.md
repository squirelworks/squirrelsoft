# Hand-off to Cursor / Codex (2026-11-15)

From 2026-11-14 the remaining SquirrelSoft work is left for Cursor or Codex to
pick up. This file says what is done, what is in flight, and what is open,
with enough detail to start any open item cold. What comes after this list,
for the whole suite (this repo, squirrelbios and squirrel-os2), is
[docs/CONTINUANCE.md](docs/CONTINUANCE.md).

Source of truth, in order: `ROADMAP.md` on `main`, then the GitHub issues
(#30, #31, #32, #28 and the hand-off tracker #131), then this file. If they
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
  0.0.3, SquirrelQR 0.1.6, SquirrelSound 0.0.13 on Play; on `main` and waiting
  for Squirrel to upload: SquirrelSound **0.0.14** (full-screen Live tab,
  Settings, octave bars, WAV, calibration) and **0.0.15** (landscape, scope,
  peak labels), SquirrelQR **0.1.7** (torch / zoom / batch), SquirrelSuica
  **0.0.13** (issuer labels, bus rows, WAON) and **0.0.14** (MIT station tables).
  Do not invent or claim Play uploads; only Squirrel publishes AABs.
- **Desktop (C, SDL2 via `libs/sqgui`):** SquirrelStation, SquirrelCAD,
  SquirrelGNSS, SquirrelSound (mic + Pulse/WASAPI/CoreAudio loopback, GUI —
  S3.2 done via #141), SquirrelSuica (PC/SC, RC-S380 over libusb, GUI, CSV —
  S3.3 done), SquirrelQR (files, PNG, X11/portal/Win/mac capture, V4L2 webcam,
  GUI, encoder — S3.4 done via #138 / #142), SquirrelCalc, SquirrelPaint,
  SquirrelGames, SquirrelDebug, SquirrelFlightLab, SquirrelISMBench (scaffold),
  SquirrelHILBench (HIL UI scaffold), SquirrelMCStubs, SquirrelKbdCfg,
  SquirrelHUDTool (scaffold), SquirrelMouseCfg (scaffold), SquirrelBusMon (scaffold), SquirrelRamTest (scaffold).
  S3, S4 and S5 are complete on `main`. PKG
  (S3.5 + S4) is on `main`.
- **Shared cores:** `libs/sqdsp`, `libs/sqfelica`, `libs/sqqr`, `libs/sqmath`, `libs/sqgui`.
- **Test gate:** `make test` on ubuntu / windows / macos (every lib and program,
  the Python SuicaAnalysis twin, and host JNI smokes for QR / Sound /
  SuicaAnalysis / GNSS on Linux) and the Android job
  (`./gradlew :app:assembleDebug :app:testDebugUnitTest` per app), in
  `.github/workflows/ci.yml` on every PR. Desktop packages: see
  `docs/packaging.md` and `.github/workflows/release.yml`.
- **Open Cursor/Codex coding items *(lane `-2eaf`)*:** Soft GNSS NTRIP/RTCM/
  Android caster wiring is done (#147–#162); ISM Soft UI S10.1–S10.4 and Eric
  companions S9.1–S9.3 (`squirrelramtest/` / #182) are on `main` or this PR.
  Remaining Soft coding: S8.2 live HID, S8.3 PSU/UPS (open PR #202), S8.4b
  live SPD/PSRAM. Next Squirrel-owned work remains **S6** repo split and
  **PLAY** uploads (`needs: squirrel`): Sound 0.0.14/0.0.15, QR 0.1.7, Suica
  0.0.13/0.0.14.
- **Agent lanes:** suite split is in [docs/AGENT_LANES.md](docs/AGENT_LANES.md)
  (`-2eaf` = this Soft/Gen2/BIOS lane; `-b55f` = OS/2 2.9 only).
- **Path to suite/OS 3.0.0:** Soft [docs/PATH_TO_3.0.0.md](docs/PATH_TO_3.0.0.md)
  is a **suite index** (PLAY/S6 + BIOS pointer). Canonical OS ladder:
  squirrel-os2 `documentation/releases/TO_3.0.0.md`
  ([os2#205](https://github.com/squirelworks/squirrel-os2/pull/205)).
  Go/no-go: Soft [#161](https://github.com/squirelworks/squirrelsoft/issues/161).
  Soft **#162** is on `main`; leave PLAY/S6 to Squirrel; suite tags are
  OS/2/BIOS only (owner publishes).

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
  libx11-dev libdbus-1-dev libpcsclite-dev pcscd libusb-1.0-0-dev xvfb pulseaudio python3
make            # every desktop program
make test       # every lib and program (see ci.yml for pcscd/vpcd and Xvfb set-up)
cd squirrelsound/android && ./gradlew :app:assembleDebug :app:testDebugUnitTest
```

Android apps: `squirrelsuica/android`, `squirrelsuica-analysis/android`,
`squirrelsound/android`, `squirrelqr/android`, `squirrelgnss/android` (JDK 17,
Android SDK 35). Without an SDK, the Gradle harnesses in the project's
`squirrelsoft-kit/tools/` compile and unit-test the Kotlin against stubs.

## Suggested order

1. ~~**GNSS-ntrip**~~ — on `main` (#147): portable `sqntrip` + `test_ntrip.c`.
2. ~~**Phone GnssMeasurement**~~ — on `main` (#148).
3. ~~**Live NTRIP transport**~~ — on `main` (#150): TCP + TLS stub over
   `sqntrip`; offline synthetic / loopback tests only.
4. ~~**RTCM decode skeleton**~~ — on `main` (#151): `sqrtcm` frame sync /
   CRC-24Q / MSM+obs header parse; synthetic unit tests only.
5. ~~**Sarah Soft follow-ups (#152 / #153)**~~ — on `main`: NTRIP SIGPIPE,
   RTCM rescan + owned payloads, phone demo-eph / TOW / `start()`, MIT
   license text; #153 multi-frame rescan / inq / copy fixup. Post-#153
   notes on `main` (#157): inq head/tail ring, prepend-overflow test,
   feed drain.
6. ~~**RTCM MSM observation fields**~~ — on `main` (#160): full DF394–DF396
   masks + MSM1–7 observation decode in `sqrtcm`; synthetic
   `tests/test_rtcm.c` only.
7. ~~**Android NTRIP caster wiring**~~ — on `main` (#162, lane `-2eaf`):
   Android UI + JNI feed `sqntrip`/`sqrtcm` (memio/loopback CI; operator dial).
8. **S6 repo split** *(Squirrel)* — prep inventory on `main`; see S6 card.
9. **PLAY uploads** *(Squirrel)* — Sound 0.0.14/0.0.15, QR 0.1.7, Suica
   0.0.13/0.0.14 (already on `main`; do not invent uploads).
10. **Suite path to 3.0.0** — Soft [docs/PATH_TO_3.0.0.md](docs/PATH_TO_3.0.0.md)
    is the suite index; OS ladder is os2 `TO_3.0.0.md` (#205). Soft coding
    is hygiene.

Done earlier (kept for history):

1. ~~SND-landscape + SND-scope + SND-peaks~~ (done on `main`: SquirrelSound 0.0.15).
2. ~~FEL-jni-smoke~~ (done: S2 JNI box).
3. ~~QR-torch-zoom + QR-batch~~ (done: SquirrelQR 0.1.7).
4. ~~**SUICA-labels + SUICA-bus + FEL-pasmo-waon**~~ (done: SquirrelSuica 0.0.13).
5. ~~**SUICA-stations**~~ (done: SquirrelSuica 0.0.14, MIT table).
6. ~~**PKG** (S3.5 + S4)~~ (done: packages + win/mac/ubuntu `make test` in CI).
7. ~~**SND-wasapi** / **SND-macos**~~ (done on `main` via #141: native loopback + CI silent capture).
8. ~~**QR-webcam**~~ (done: #138, V4L2 + synthetic CI frame).
9. ~~**QR-wayland / QR-capture-win-mac**~~ (done: #142, portal / GDI-DXGI / ScreenCaptureKit).

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

### SND-wasapi / SND-macos — system audio on Windows and macOS  `status: done`
- `live_audio_wasapi.c` (WASAPI render loopback) and `live_audio_coreaudio.m`
  (CoreAudio device list + ScreenCaptureKit when allowed, silent fallback for
  CI) behind the same `live_audio` interface. `squirrelsound devices` lists
  `monitor Monitor of …`; `--source loopback` captures. When the host has no
  render endpoint (typical `windows-latest`), WASAPI lists/opens
  `Monitor of Silent Output` with timed silence — same CI shape as CoreAudio.
  `tests/test_capture.sh` exercises that path; soft-skips only if no monitor
  is listed and `REQUIRE_NATIVE_LOOPBACK` is unset.

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

### QR-webcam — desktop camera capture  `status: done` (#138)
- `--webcam` / `squirrelqr devices` on V4L2 (`webcam_v4l2.c`); Win/mac link
  `webcam_stub.c` (`mf-stub` / `avf-stub`). CI: `test_webcam` and
  `webcam_smoke.sh` decode a synthetic frame via `--device @FILE` or
  `QR_WEBCAM_FRAME` (no real camera).

### QR-wayland / QR-capture-win-mac — screen capture  `status: done` (#142)
- Linux: `capture_portal.c` (xdg-desktop-portal Screenshot over D-Bus) plus
  X11; `capture_linux.c` picks portal when `WAYLAND_DISPLAY` is set.
- Windows: `capture_win.c` (GDI BitBlt, DXGI Desktop Duplication fallback).
- macOS: `capture_mac.m` (ScreenCaptureKit / `SCScreenshotManager`).
- CI: `test_capture` + `capture_smoke.sh` decode a synthetic frame via
  `QR_CAPTURE_FRAME` on every OS (same pattern as the webcam synth path).

### PKG — Windows/macOS/Linux packages in CI (S3.5, S4)  `status: done`
- Done on `main` (#137): `scripts/release/package_desktop.sh` builds zip/MSI
  (Windows), `.app` in `.dmg` (macOS), AppImage + `.deb` (Linux) for every
  desktop program; `.github/workflows/release.yml` and `ci.yml` run `make test`
  on windows-latest / macos-latest / ubuntu-latest; draft Releases attach the
  packages (unsigned OK). See `docs/packaging.md`. Unblocked QR-capture-win-mac
  (#142) and SND-wasapi/macos (#141).

### GNSS-android-jni — SquirrelGNSS on Android (CONTINUANCE)  `status: done` (#146 on `main`)

### GNSS-android-meas — phone / synthetic measurement feed  `status: done` (#148 on `main`)
- Packed obs → PVT via JNI (`nativeFillDemoEpoch` / `nativeSolveEpoch`),
  `SyntheticMeasFeed` for CI, optional `PhoneGnssFeed`, Kotlin packing /
  `GnssRawMapper`, JVM + instrumented synthetic tests.

### GNSS-ntrip — NTRIP client skeleton (CONTINUANCE)  `status: done` (#147 on `main`)
- Portable `sqntrip` in `squirrelgnss/` (`include/sqntrip.h`, `src/sqntrip.c`):
  GET request builder (NTRIP 1/2 headers, Basic auth), ICY/HTTP response
  parser, sourcetable `STR;` parse. Unit tests in `tests/test_ntrip.c` feed
  synthetic caster bytes only — no network in CI. Wired into `make test`.

### GNSS-ntrip-live — live NTRIP TCP transport (CONTINUANCE)  `status: done` (#150 on `main`)
- `SqntIo` / `SqntClient` / `SqntTcp` in `sqntrip_transport.c`: injectable I/O,
  memory caster, portable TCP (Winsock on Windows), loopback listen/accept.
  TLS flag fails cleanly (`sqnt_tls_supported() == 0`; no third-party TLS).
  Tests: synthetic memio + `127.0.0.1` loopback only. CLI: `ntrip-synth`
  (CI) and `ntrip HOST:PORT` (operator; not dialled from CI). Design note:
  `squirrelgnss/docs/ntrip-transport.md`.

### GNSS-rtcm — RTCM 3 decode skeleton (CONTINUANCE)  `status: done` (#151 / #153 on `main`)
- Portable `sqrtcm` (`include/sqrtcm.h`, `src/sqrtcm.c`): preamble sync,
  CRC-24Q, message/station header parse for legacy obs, station ARP and MSM;
  streaming `SqrtcmFramer` + `sqrtcm_framer_feed` for NTRIP body bytes.
  #153: rescan after bad CRC, owned payloads on multi-frame feed,
  `SQRTCM_INQ_MAX` / `rescan_drop`, GLONASS day+ms epoch documented.
  Post-#153: head/tail inq ring, prepend-overflow drop test. Unit tests in
  `tests/test_rtcm.c` (synthetic frames only). Design note:
  `squirrelgnss/docs/rtcm-decode.md`.

### GNSS-rtcm-msm — MSM observation fields (CONTINUANCE)  `status: done` (#160 on `main`)
- Full MSM1–7 observation decode in `sqrtcm`: DF394–DF396 masks, DF397–DF399
  satellite rough ranges/rates, per-cell DF400–DF408 / DF420 fields;
  `sqrtcm_pack_msm` / `sqrtcm_parse_msm` + metre helpers; 73-bit MSM header
  through DF418. Synthetic round-trips in `squirrelgnss/tests/test_rtcm.c`
  (MSM4 / MSM7, invalid sentinels, framer glue). Design note cites
  RTCM 10403.x §3.5.12–3.5.13.

### GNSS-android-caster — Android NTRIP caster wiring  `status: done` (#162 on `main`, lane `-2eaf`)
- Wire `sqntrip` + `sqrtcm` into the SquirrelGNSS Android UI so a mount can
  be dialled and MSM corrections framed/parsed. JNI links the same C cores;
  CI uses memio + `127.0.0.1` loopback only (host `make test-jni`, unit +
  instrumented tests). Operator dial is UI-only. Design note:
  `squirrelgnss/docs/android-ntrip.md`.

### Soft-Sarah-followups — NTRIP / RTCM / phone / MIT hygiene  `status: done` (#152 / #153 on `main`)
- #150 SIGPIPE, #151 framer notes, #148 demo-eph UI + TOW null + `start()`
  honesty, #140 MIT license text. #153 on `main`: RTCM multi-frame rescan
  delivery, `SQRTCM_INQ_MAX` + `rescan_drop`, `payload_owned` /
  `sqrtcm_msg_payload` / `sqrtcm_msg_assign`. Post-#153 notes on `main`
  (#157): inq head/tail ring (O(1) take), prepend-overflow whole-tail drop
  test, `framer_feed` drain only when `n==0`.


### FLIGHTLAB-scaffold — ISM Fighter Jet Flight Lab  `status: done` (#167)
- Desktop app `squirrelflightlab/` scaffold on `main`.

### FLIGHTLAB-ism-params — load ISM params + FCS co-check  `status: done` (#169)
- S7.2: `fl_ism` / `fl_fcs`, fixtures under `fixtures/ism-fighter/`, CLI
  `ism-load` and `fcs-check` with pass/fail report. Soft #168 acceptance for
  load + scripted FCS co-check. Victor SoT files: ISM #88. Soft does not design
  airframe hardware.

### FLIGHTLAB-ism-interfaces — load fighter-jet/interfaces YAML  `status: done` (#178)
- S7.2b / Soft #170: `fl_yaml` + `fl_iface`, Soft allowlist
  `schemas/ism-fj-1.0.0/`, CI snapshot `fixtures/ism-interfaces/` from ISM
  `main`, CLI `ism-interfaces`. Schema-version gate; loud missing/unknown
  top-level keys. Soft does not fork SoT — refresh path in fixtures README.

### FLIGHTLAB-nested-allowlists — Soft #177  `status: done` (#195)
- S7.2g: Soft schema `nested:` closed maps/seqs (`surfaces[]`, `envelope`,
  `stations[]`, `separation_envelopes.defaults[]`, …). Unknown nested key or
  missing required nested path → hard error with dotted/indexed path.
  Numerics remain Soft #174 / #172 (NR-cited; no NR code copied).

### SQUIRRELNUM + Flight Lab RK/units — Soft #174  `status: done` (#185)
- S7.2c: `libs/squirrelnum` (RK4, DP5(4), Brent, spline, LA, quaternions);
  Flight Lab `fl_dyn` defaults to RK4; `docs/units-and-scale.md` + mm/g
  mix-up tests. Fred D1 risks (Euler + mm/g) folded in per Sarah.

### FLIGHTLAB-verify — analytic verification Soft #172  `status: done` (#191)
- S7.2d: `fl_verify` analytic cases + optional AVL/OpenVSP harness (skip if
  absent); provisional label; `make verify`. NESC CSV / F-16 TP-1538 deferred
  (licence check before vendoring).

### FLIGHTLAB-viability — Soft #176  `status: done` (#193)
- S7.2e: `fl_viability` evaluates ISM `viability-criteria.yaml` gates vs class
  bands; MD+JSON reports; `viability-report` CLI; optional tool xcheck skip.

### FLIGHTLAB-viability-soft-recompute — Soft #176 follow-up  `status: done` (#196)
- Soft recompute of formerly deferred gates: G-BREGUET (Breguet + Brent L/D
  oracle, NR Ch. 9.3 via squirrelnum; no NR listings), G-CG travel %MAC,
  G-STRUCT-MARGIN ultimate = limit×factor.
- **Gaps (file Soft issues if missing):** numeric AVL/OpenVSP/CalculiX table
  compare; Eric scale static / tunnel / RC FC log correlation; NESC CSV /
  F-16 TP-1538 (licence check before vendoring).

### FLIGHTLAB-tunnel-csv — S7.3 replaceable aero + tunnel CSV  `status: done` (#199)
- Soft tunnel CSV → `FL_AERO_PROVIDER_TUNNEL_TABLE`; CL/CD/Cm(α) via
  `sqn_lerp` (NR Ch. 3.1); linear CL/Cm fit (NR Ch. 15.1 class ideas, Soft
  original); `tunnel PATH` correlate vs parametric scaffold.
- **Gaps:** real Eric/Victor tunnel logs (not Soft synthetic); CFD mesh import
  still planned; spline tunnel tables (NR Ch. 3.3) optional follow-up.

### FLIGHTLAB-strip-chart — S7.4 sqgui strip chart  `status: done` (#200)
- `fl_chart` + CLI `strip-chart`: headless `sqgui` canvas of sim history
  (V/nz/α/θ); NR Ch. 3.1 lerp resample onto columns; optional PPM snapshot.
- **Gaps:** interactive SDL strip window (optional); live HIL feed into chart
  (partially unblocked by S7.5 mock path).

### FLIGHTLAB-hil-mock — S7.5 Soft mock PCB HIL  `status: done` (this PR)
- `fl_hil` Soft SQHIL1 framing (HELLO/ACK/SYNC/CH/BYE); `hil mock` and
  `fixtures/hil/*.sqhil`; correlate host↔board with `sqn_lerp` (NR Ch. 3.1;
  Soft original, no NR listings). Live `/dev`/`serial:` refused until Victor
  freezes avionics PCB ICD; points at squirrel-os2#225 for host transport.
- **Gaps (file Soft/ISM issues if missing):** live USB-CDC/UART framing once
  ICD lands; feed Soft HIL samples into `fl_chart` live strip; refresh fixture
  from ISM harness notes; `gh issue create` may 401 — gaps recorded here / PR.

### FLIGHTLAB-loadout — Soft #173  `status: done` (#194)
- S7.2f: `fl_loadout` validates station×adapter×store against ISM interface
  fixtures; mass/CG/asymmetry + release envelopes; `loadout-check` CLI;
  legal/illegal fixtures under `fixtures/loadouts/`.

### ISMBENCH-scaffold — keyboard/mouse HID harness  `status: done` (#169)
- S8.1: `squirrelismbench/` matrix + mouse self-tests. Eric interface docs:
  ISM #89 / #90. Live USB and other lab benches are S8.2+.

### ISMBENCH-ram-typewriter — Soft #182 fixture + typewriter matrix  `status: done` (#197)
- S8.1b: typewriter 4×12 fixture + `typewriter-test` (ISM #4 / open PR #159).
- S8.4a: `memory-test` — SPD XOR Soft contract (I2C `0x50`) + walking /
  checkerboard / Park–Miller LCG bank patterns (NR Ch. 7.1 class oracle;
  Soft original, no NR listings). Soft #182 acceptance for **fixture harness**
  via `squirrelismbench` (test-equipment lane). Companion Soft UI app remains
  `-2eaf` after kbd/mouse configurators.
- **Gaps (file Soft/ISM issues if missing):** live USB-CDC SPD line parse
  (`ISM SPD: …`); live PSRAM march once Soft has a host path; refresh
  typewriter fixture when ISM #159 merges; Soft #182 companion UI is `squirrelramtest/` (S9.3; this PR).

### ISMBENCH-printer-plotter + SHIPBENCH — Soft lab expansion  `status: done` (#198)
- S8.4b′: `printer-test` (ISM #5 Steve GPIO/VID map) + Soft-proposed
  `plotter-test` (ISM #6; NR Ch. 3.1 lerp via squirrelnum).
- S8.5: `squirrelshipbench/` warship hydro + board allowlist (ISM #43) and
  bomber teaching heat-flux (ISM #37); Soft Simpson I_T (NR Ch. 4.1 class).
- **Gaps:** live printer USB; plotter fab SoT when ISM #6 leaves Planned;
  warship live board CDC; refresh Soft fixtures from ISM YAML when SoT freezes;
  could not `gh issue create` (401) — gaps recorded here / in the PR.

### KBDCFG-scaffold — Eric keyboard layout + firmware cfg  `status: done` (#184)
- S9.1: `squirrelkbdcfg/` loads 60% fixture, prints layout, SQKB1 round-trip.
  Mouse settings #181 and RAM self-test #182 follow in Eric order (now on `main` / this PR).

### MOUSECFG-scaffold — Eric mouse DPI + button settings  `status: done` (#181)
- S9.2: `squirrelmousecfg/` loads mouse fixture, shows/edits DPI steps and
  button remap, SQMS1 Soft config round-trip. Bench harness stays in
  `squirrelismbench/`. Keyboard #180 done; RAM #182 companion is `squirrelramtest/` (S9.3).

### HILBENCH-scaffold — fighter HIL bench UI  `status: done` (#189, lane `-2eaf`)
- S10.1 / ISM #80: `squirrelhilbench/` mock profile connect, actuator /
  inceptor / bus health, canned stimulus, Flight Lab–compatible
  `hil-*-replay.csv` export. Harness SoT linked to ISM `fighter-jet/`.
  Weapons boundary: stores display only. Does not duplicate Flight Lab.

### MCSTUBS-scaffold — mission-computer stubs for fighter ICD  `status: done` (#186, lane `-2eaf`)
- S10.2 / ISM #81: `squirrelmcstubs/` MC, SMC/SMS (display+loadout+safeing only),
  and one display-page publisher; fixture catalog; deterministic ticks; checks
  `tests/test_mcstubs.c`, `tests/cli_smoke.sh`. Stubs are not flight software.
  Weapons boundary: `live_fuzing_in_repo` stays false.

### HUDTOOL-scaffold — ISM HUD/HMI symbology CLI  `status: done` (#187)
- S10.3: `squirrelhudtool/` authors HUD + MFD pages, exports SQHUD/JSON;
  fixtures under `fixtures/pages/`; checks `tests/test_hudtool.c`,
  `tests/cli_smoke.sh`. ISM #82 / `fighter-jet/avionics/hmi-hud.md`. No
  weapons internals — stores station flags are display symbology only.

### BUSMON-scaffold — ISM avionics data-bus analyzer  `status: done` (#190)
- S10.4: `squirrelbusmon/` decode / filter / replay against ICD catalog fixtures;
  checks `tests/test_busmon.c`, `tests/cli_smoke.sh`. Protocol SoT in ISM;
  SMS/stores topics opaque display fields only. Replay hook for Flight Lab / HIL.



### RAMTEST-scaffold — Eric RAM-module self-test utility  `status: done` (this PR)
- S9.3: `squirrelramtest/` loads ISM-MEM fixture, prints SPD Soft XOR contract,
  runs fixture bank patterns, writes pass/fail artifact; checks
  `tests/test_ramtest.c`, `tests/cli_smoke.sh`. Bench harness remains
  `squirrelismbench/` `memory-test`. Live I2C SPD / PSRAM → Soft #203 / #204
  (S8.4b gaps).

### PLAY-0.0.14 — upload SquirrelSound 0.0.14  `needs: squirrel`
- Build the AAB locally with the upload key and upload it to closed testing
  (`scripts/` has the Play helpers). Do not commit or attach the AAB anywhere.

### S6 — per-app repos  `needs: squirrel` (prep done on `main`)
- **Prep on `main` (#139):** `scripts/s6_repo_split_prep.sh` (path lists and
  dry-run `filter-repo` lines), `docs/s6-repo-split.md` (inventory, owner
  checklist, umbrella README link draft), `docs/s6-ci-stubs/` (per-future-repo
  CI). No remotes are created or deleted by agents.
- **Squirrel still does:** create the GitHub repos, run `git filter-repo` on
  mirror clones, wire CI from the stubs, resolve the SuicaAnalysis →
  `felica_parse.py` cross-import, then update the umbrella README links.

## Issues to close or keep

- #24, #27, #29 (implementation done), #85, #86, #87 (S3.3 steps merged), and
  the probes #83, #84 are closed with this hand-off.
- **#28 can close** if still open (0.0.15 landscape + scope + peaks are on
  `main`; implementation done — only a real-device phone check remains for
  Squirrel and is non-blocking).
- **#30 / #31 can close** (S3 native front-ends / desktop parity complete;
  S3.2 WASAPI/macOS merged via #141).
- #152 / #153 (Sarah RTCM follow-ups) are on `main` (merged via #152–#155).
- #32 (S6 epic) and #88 (forward plan) stay open. Tracker #131 can be updated
  to show only Squirrel-owned S6 / PLAY lines.
