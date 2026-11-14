# SquirrelSoft Suite: continuing the work (2026-11-14)

This is the forward plan for the whole suite after the hand-off to Cursor /
Codex: **squirrelsoft** (apps and shared cores, this repo), **squirrelbios**
(firmware) and **squirrel-os2** (the operating system). Each repo's
`HANDOFF.md` lists the open items with files, acceptance checks and tests;
this document is about what comes after those lists, in what order, and what
could go wrong.

## 1. Where the suite stands

| Repo | Released | `main` beyond the release | Gate |
|---|---|---|---|
| squirrelbios | v2.1.1 (2026-11-13) | D4 shared drivers, network/HTTP boot, NVRAM + authenticated variables, TPM, SMM/EINJ, IPMI, all of Phase E (POWER, SPARC, m68k, MIPS, LoongArch, s390x), Raspberry Pi on QEMU | `make test`, ~260 QEMU cases |
| squirrel-os2 | 2.1.0 (2026-11-13) | 2.2 windows, most of 2.3, 2.4 ring 3, 2.7 FAT/long names, 2.8 long-mode entry + SMP, H.1–H.6, 3.0 network + sound, every game, the calculator suite, Linux/Win32/DOS/OS2/FreeBSD programs | `tools/regress.sh`, ~750 checks |
| squirrelsoft | per app on Play closed testing | SquirrelSound 0.0.14; desktop Suica/QR/Sound parity; Calc, Paint, Games, Debug | `make test` + Android unit tests |

The three repos already form one stack: SquirrelBIOS boots Squirrel-OS/2
(BIOS, CSM and UEFI paths), apps are built in squirrelsoft first as portable
C cores and then ported, and SquirrelDebug debugs the OS over QEMU's gdbstub.
The best next steps make that stack tighter rather than wider.

## 2. Next milestones

### M1 — Ship what `main` already has (first)
- SquirrelBIOS **v2.2.0**, Squirrel-OS/2 **2.3.0** (after 2.3-control and
  2.3-bevel-default), SquirrelSound **0.0.14** on Play. Release notes written
  by the agent, published by Squirrel.
- A **suite compatibility matrix** in each release note: which OS/2 release
  boots on which BIOS release, by which path (BIOS, CSM, UEFI), on which QEMU
  machine. Make squirrel-os2's `uefi-squirrelbios` check reliable (it is
  skipped in CI today because it sometimes misses `BOOTX64.EFI`) and run it
  against the latest SquirrelBIOS release image.

### M2 — "Warp 4 generation" (OS/2 2.5–2.7 + W.2–W.6)
- WarpCenter bar, settings notebooks, drag and drop, shadows, folder views,
  templates. This is what makes the OS feel like Warp rather than Windows 3.1.
- Port `libs/sqgui`'s back end to the Squirrel-OS/2 window manager so
  desktop programs (SquirrelCAD, Station, GNSS, QR) run on the OS from the
  same C sources instead of being rewritten in NASM (see decision 3a).

### M3 — "64-bit Warp" (OS/2 3.0.0)
- 2.8-pae → 2.9-lm-kernel → 2.9-syscall64, then accounts, permissions,
  services, event log, task manager, themes.
- On the firmware side: Squirrel-OS/2's 64-bit kernel boots natively on
  SquirrelBIOS `x86_64-uefi` with Secure Boot on, its loader signed with the
  project's own keys and measured into the TPM. That is a whole verified
  chain of original code, firmware to desktop, which very few hobby stacks have.

### M4 — SquirrelSoft 1.0
- Each Android app moves from closed testing to production on Play, with the
  open hand-off cards closed (landscape/scope in Sound, torch/zoom/batch in
  QR, PASMO/WAON/bus rows in Suica).
- Signed desktop packages for Windows, macOS and Linux from CI.
- The per-app repo split (S6), done by Squirrel with the tree prepared by an agent.

### M5 — Real hardware
- One x86 board (GA-G41M-ES2L as a coreboot payload), one Raspberry Pi 4,
  and one ordinary PC booting Squirrel-OS/2 from a USB stick through
  SquirrelBIOS's CSM. Results recorded as post-merge notes, never as CI gates.

## 3. Decisions worth making early

### 3a. Write new kernel code in freestanding C
The OS/2 kernel is NASM, and single files are now past 1,400 lines
(`kernel/control.asm`). The 64-bit kernel (2.9) is a natural point to allow
freestanding C (`-ffreestanding -mno-red-zone -fno-pic`, linked into the flat
image) for new subsystems while keeping NASM for boot, entry stubs and the
existing code. The payoff is large: squirrelsoft's cores (`sqmath`, `sqqr`,
`sqdsp`, `sqfelica`, `sqgui`) and SquirrelBIOS's `common/efi` drivers are
already freestanding C and could be linked in rather than re-implemented.
Recommendation: decide this before 2.9-lm-kernel starts, and record it in
`documentation/architecture/overview.md`.

### 3b. One shared freestanding library instead of three copies
The suite now has several independent implementations of the same things:
FAT (SquirrelBIOS `common/efi/fat.c`, OS/2 `kernel/fat.asm`), HTTP/TCP/DHCP
(BIOS `common/efi/net_*.c`, OS/2 `kernel/tcp.asm`/`dhcp.asm`/`http.inc`),
PNG/zlib (`sqqr`, SquirrelPaint), PE loaders (BIOS, OS/2 `winrun`), and
AHCI/NVMe/xHCI drivers (BIOS, OS/2 H.3–H.6). Once 3a is decided, move the C
ones into a small `libs/sqcore` (or a `squirrel-common` repo pulled in by
`git subtree`) with host-side unit tests, and let the BIOS and the OS consume
it. Bugs then get fixed once.

### 3c. Keep the docs honest automatically
During this hand-off the roadmaps were several merged PRs behind `main`
(boxes for EINJ, UAS, A.6, P.9 and others were done but unticked). Add a small
`tools/docs_lint.py` to each repo's CI: every `- [x]` box in `TODO.md` that
names a check must find that check in `tools/regress.sh` / `tests/smoke.sh`,
and every check named by an open box must not exist yet. It is cheap and
stops the drift that makes hand-offs expensive.

### 3d. Make the test gates scale
`regress.sh` runs about 750 checks in one long serial QEMU session, and
`make test` about 260 cases. Shard both into a CI matrix (by section or by
disk image), keep a per-check time budget, and run timing-sensitive cases
with QEMU `-icount shift=auto` so they are deterministic (the `nvme-flake`
and `grapher-flake` branches show this has already bitten).

## 4. Ideas to take it further

### SquirrelBIOS — Phase G, "firmware as a product"
- **Setup UI**: a graphical setup and boot menu on GOP in the Warp look
  (boot order, Secure Boot keys, TPM clear, date and time, NVRAM reset).
- **Capsule updates**: `UpdateCapsule` + ESRT so `fwupd` can update the
  firmware, with the capsule signed by the project key.
- **Fuzzing**: host builds of the parsers that read untrusted input (FAT,
  PE/COFF, Authenticode/PKCS#7, DHCP/TFTP/HTTP, USB descriptors, the device
  tree reader) under libFuzzer/AFL in a nightly job.
- **More boards**: VisionFive 2 and HiFive Unmatched have research notes;
  each needs a `CONFIG=` with its UART, memory and SD controller.
- **Measured hand-off to the OS**: pass the TPM event log to Squirrel-OS/2 and
  let the OS show it (a "boot security" page in the Control Panel).

### Squirrel-OS/2
- **Self-hosting**: an original NASM-compatible assembler subset running on
  the OS, then building the kernel on the OS itself. A small C compiler is the
  stretch after that. Nothing says "real OS" like rebuilding itself.
- **Package manager earlier than 4.x**: a simple signed package format on
  SQFS (signatures with the same keys as Secure Boot) so apps and games
  install without rebuilding the disk images.
- **Japanese**: add `ja` beside `en`/`la`/`tlh` with an original bitmap CJK
  subset; it suits the Suica users and exercises the font pipeline.
- **Suica on the OS**: an RC-S380 USB driver in the OS plus `sqfelica` (via
  3a) turns "tap a transit card on Squirrel-OS/2" into a demo.
- **Screenshots from CI**: the regression already reads framebuffers back;
  dump them as PNGs per release and publish a gallery on
  squirelworks.github.io automatically.
- **Foreign programs, bounded**: define each personality's "done" as a fixed
  list of real test programs (for example busybox static, a MinGW Win32 GUI
  sample, a DJGPP or Turbo-C DOS program) so P.3–P.12 cannot grow without end.
- **Networking out**: a TCP server side, a tiny HTTP server and a remote
  shell; TLS only when an original, carefully tested implementation is
  affordable (it is a large crypto project; do not bolt on someone else's).

### SquirrelSoft apps
- **SquirrelGNSS on the phone**: Android exposes raw GNSS measurements
  (`GnssMeasurement`); feed them to the existing `sqgnss` core over JNI and add
  an NTRIP client for corrections: RTK-grade positioning in an app, reusing
  code that is already tested on desktop.
- **SquirrelCalc on Android**: `sqmath` + a native front-end gives a Desmos /
  TI-Nspire-class calculator on the phone from the same core.
- **SquirrelQR beyond QR**: original decoders for Micro QR / rMQR, Data Matrix,
  Aztec and 1D codes (EAN-13, Code 128) in `sqqr`; that makes the scanner
  useful for shopping and parcels.
- **SquirrelSound measurement tools**: an SPL meter with A/C weighting using the
  per-device calibration that already exists, a tone/sweep generator and a
  frequency-response measurement.
- **SquirrelSuica insights**: monthly spend and commute detection computed on
  the phone only (no upload), CSV for bookkeeping. An iOS version is possible
  (Core NFC reads FeliCa) but needs an Apple developer account (Squirrel).

## 5. Risks and how to keep them small

| Risk | Mitigation |
|---|---|
| Velocity outruns review: about a hundred PRs merged in a few days, with one owner publishing. | Keep CI gates strict and sharded (3d); every PR reports its test count; group work into sizeable functional PRs as the project rules ask. |
| Docs drift from `main`. | `docs_lint.py` in CI (3c); hand-off trackers ticked in the same PR as the code. |
| NASM kernel complexity, especially for 64-bit work. | Freestanding C for new subsystems (3a); module boundaries; symbol maps for SquirrelDebug. |
| QEMU is the only truth; real machines differ (timing, ACPI quirks, USB). | Real-hardware notes per release (M5); deterministic `-icount` for timing tests. |
| "Original code only" erodes when agents paste familiar snippets. | Cite the spec section in each design note; reviewers reject code that mirrors a known project's structure; never vendor. |
| Real card data leaks into the tree. | Synthetic fixtures only; add a CI check that every fixture JSON carries `"synthetic": true` and refuses files that do not. |
| Play policy changes (yearly target-API deadline, data safety forms). | Bump `targetSdk` each year in one PR across the four apps; keep `docs/play-app-content-checklist.md` current. |
| Signing keys in shared places. | Keep the upload keystore in a password manager, not in shared folders or chat; rotate the upload key through Play if a copy was ever shared. |
| Foreign-personality scope creep. | Fixed test-program lists per personality (section 4). |

## 6. Suggested order across the suite

1. Releases for what is already on `main` (M1), including the compatibility matrix.
2. Short open cards that finish milestones: OS/2 2.3-bevel-default and
   2.7-diskutil; BIOS F3-DNS and F2-CXL; Sound landscape/scope/peaks; FEL-jni-smoke.
3. Decide 3a and 3b, then add `docs_lint.py` (3c) and sharded CI (3d).
4. OS/2 Generation 2 (2.5, 2.6, W.2–W.6) with the `sqgui` back end for the OS.
5. OS/2 64-bit (2.8-pae, 2.9) together with the signed UEFI boot on SquirrelBIOS (M3).
6. SquirrelSoft 1.0: packaging, desktop capture on every OS, Play production (M4).
7. Real hardware (M5) and Phase G firmware features.
8. The idea list in section 4, as each milestone above frees capacity.

## 7. Working with Cursor / Codex on this

- Give each agent session one card from a repo's `HANDOFF.md` and the repo's
  `AGENTS.md` (Codex and Cursor both read it automatically).
- Run agents in parallel across repos or across clearly separate files; inside
  squirrel-os2 the shared files (`kernel/pm.asm`, `kernel/shell.asm`,
  `tools/regress.sh`, `localization/`) conflict easily, so serialise work that
  touches them or have one integrating branch per functional group.
- Every PR: based on `main`, never stacked, sizeable and functionally grouped,
  targets `main`, mergeable, CI green, boxes ticked in the same PR.
- Only Squirrel tags, publishes, uploads to Play, creates repos or deletes branches.
