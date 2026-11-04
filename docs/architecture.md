# SquirrelSoft architecture: shared cores and platform front-ends

Issue 09 / roadmap S2 and S6. SquirrelSoft is a set of apps that will later move to their own repositories. It is also where Squirrel OS/2's userland apps get built first, as desktop programs, before being ported natively. To make both possible, every app is split into:

- a **portable core** in `libs/`: C11, no OS calls, and I/O through caller-supplied buffers or callbacks. It compiles for Linux, Windows, macOS, the Android NDK and Squirrel OS/2.
- **thin front-ends** per platform: Android (Kotlin, later calling the core via JNI), desktop (C with SDL2), and Squirrel OS/2 (its own wm/gfx).

## Target layout

```
libs/
  sqdsp/     FFT, windows, PSD, pitch/THD (SquirrelSound)       ← moved here (S2 first step)
  sqfelica/  FeliCa command builder/parser, Suica/PASMO/nanaco/Edy/WAON history  ← added (S2 second step)
  sqqr/      original QR decoder: image locate (finders, alignment, perspective) + grid decode + Reed–Solomon
  sqmath/    expression engine / CAS core (SquirrelCalc)
  sqgui/     tiny immediate-mode widgets over SDL2, portable to Squirrel OS/2 wm/gfx
<app>/       today's per-app folders (squirrelsound/, squirrelsuica/, ...)
  android/   Gradle project (already self-contained)
  src/ ...   desktop front-end and glue
```

The app folders stay where they are for now. Moving them under `apps/` is churn with no benefit until a split happens.

## Rules for a core library

1. C11 only, built with `-std=c11 -Wall -Wextra`, and it must compile warning-free.
2. No OS calls: no files, sockets, threads or clocks. The front-end does I/O and passes buffers in. The C standard library is fine (`math.h`, `string.h`, `stdlib.h` for allocation), but keep the list short so a small Squirrel OS/2 libc shim covers it.
3. A public header in `include/`, sources in `src/`, and unit tests in `tests/`. `make` builds `build/lib<name>.a`, and `make test` runs the tests. The top-level `make test` runs every lib first.
4. Consumers add `-I../libs/<name>/include` and link `../libs/<name>/build/lib<name>.a`, delegating the build with `$(MAKE) -C`. They never reach into another app's folder.
5. Original code only. Do not copy from Numerical Recipes, Spectroid, nfcpy, metrodroid, ZXing or any other third-party source.

## Kotlin and C parity

Until the Android apps call the cores through JNI, Kotlin keeps its own implementation, and a **parity fixture** keeps the two in step:

- `libs/sqdsp/tools/gen_parity.c` writes `libs/sqdsp/fixtures/parity_1024.txt`: input samples, Hann window and FFT magnitudes from the C core.
- `make -C libs/sqdsp test` checks that the committed fixture still matches the C code, within a 1e-9 tolerance for libm last-bit differences.
- `FftParityTest` in SquirrelSound's Android unit tests runs `Fft.hann` and `Fft.forward` on the same samples. They must agree within 1e-4 of the peak, and CI runs this test.
- After an intentional change to the core, run `make -C libs/sqdsp fixture` and commit the result.

`sqfelica` uses the same idea with Python as the reference, since `squirrelsuica/felica_parse.py` is the parser the desktop tool and its tests already trust:

- `libs/sqfelica/tools/gen_fixture.py` runs `felica_parse.py` on synthetic history blocks (the demo cards, edge cases and seeded random blocks) and on balance blocks, and writes `libs/sqfelica/fixtures/parity.txt`.
- `make -C libs/sqfelica test` regenerates the fixture into `build/`, diffs it against the committed copy, and then runs the C parser over the same blocks. Every field must match: kind, date, time, fare, balance, terminal, process and the entry/exit line/station codes.
- So a change to either parser fails `make test` until the other side matches and `make -C libs/sqfelica fixture` is rerun.
- Station names stay in the front-ends. The C core reports raw line/station codes plus `sqf_station_area`, and each front-end looks names up in its own copy of the station table.
- Real card dumps from SquirrelSuicaAnalysis (S1.3) get added as fixture groups once they arrive, and the Kotlin parser joins the same fixture.

## Split checklist (S6, done by the owner)

An app is ready to move to its own repository when all of these hold:

- [ ] Its folder builds and tests on its own: `make -C <app> test`, plus `./gradlew` in `<app>/android`.
- [ ] It includes nothing from other app folders, only from `libs/`.
- [ ] It has its own CI job.
- [ ] The cores it uses are listed, and get vendored or submoduled at split time.
- [ ] Its Play listing, applicationId and versionCode history are noted, because the applicationId never changes.

The owner creates the new repositories and runs `git filter-repo` to keep history. Claude only prepares the tree.

## Next steps

- `libs/sqfelica`: add real card dumps to the fixture (S1.3 feeds it), a Kotlin parity test, and a station table loader that takes a caller buffer.
- JNI: SquirrelSound calls `sqdsp` through the NDK, keeping `Fft.kt` as the fallback until parity holds on devices.
- `libs/sqgui` and `libs/sqmath` for SquirrelCalc and the Squirrel OS/2 apps (S5).
