# Suite agent lanes (2026-11-15 Asia/Tokyo)

Two Cursor agents are active on the Squirrel suite. Branch suffixes identify
ownership. **Do not take a box that the other lane has claimed.** Finish or
rebase an existing PR; never redo it on a new branch.

**Living OS/2 lane issue (canonical):** [squirrel-os2#201](https://github.com/squirelworks/squirrel-os2/issues/201)
(chosen over #200 by rock–paper–scissors; see [#202](https://github.com/squirelworks/squirrel-os2/issues/202)).
If this file and #201 disagree, prefer #201 for claim lines and `main` for code.

**Path to 3.0.0:** Soft **[PATH_TO_3.0.0.md](PATH_TO_3.0.0.md)** is a **suite
index** only. Canonical OS ladder / resume walkthrough:
squirrel-os2 [`documentation/releases/TO_3.0.0.md`](https://github.com/squirelworks/squirrel-os2/blob/cursor/release-coord-b55f/documentation/releases/TO_3.0.0.md)
([os2#205](https://github.com/squirelworks/squirrel-os2/pull/205)). CONTINUANCE
M3 = destination **3.0.0**. Ask `-b55f` to confirm OS claim rows on #201 /
`TO_3.0.0.md` when credits reset.

| Lane | Branch suffix | Scope |
|------|---------------|-------|
| **A** | `-2eaf` | Gen2 desktop finish; Soft ISM fighter UI (#80–#83) + Eric companions; BIOS leftovers; CI/rebase hygiene |
| **B** | `-b55f` | **2.9-lm-kernel** tick ([os2#232](https://github.com/squirelworks/squirrel-os2/pull/232)) then **2.9-syscall64** — Soft does **not** open competing LM / syscall64 PRs |

Shared files (`TODO.md`, `ROADMAP.md`, `HANDOFF.md`, check lists): rebase onto
`main` after the other merges; never stack feature branches. Prefer one agent
editing the docs boxes for its own cards per PR.

---

## Near-term finish list (then owner releases)

Land these **before** calling the current-main OS/2 cut ready. Full OS
checklist and tag order: os2
[`TO_3.0.0.md`](https://github.com/squirelworks/squirrel-os2/blob/cursor/release-coord-b55f/documentation/releases/TO_3.0.0.md)
(Soft index: [PATH_TO_3.0.0.md](PATH_TO_3.0.0.md)).

| Repo | PR | Lane | Status |
|------|----|------|--------|
| os2 | [#184](https://github.com/squirelworks/squirrel-os2/pull/184) icons/drag | `-2eaf` | open — finish |
| os2 | [#197](https://github.com/squirelworks/squirrel-os2/pull/197) Gen2 Sarah | `-2eaf` | open — finish |
| os2 | [#204](https://github.com/squirelworks/squirrel-os2/pull/204) lm-boot64 | `-b55f` | open — rebase when CONFLICTING |
| os2 | [#205](https://github.com/squirelworks/squirrel-os2/pull/205) release coord docs | `-b55f` | open — docs |
| soft | [#162](https://github.com/squirelworks/squirrelsoft/pull/162) Android NTRIP | `-2eaf` | **merged** |
| bios | [#189](https://github.com/squirelworks/squirrelbios/pull/189) capsule/ESRT | `-2eaf` | **merged** |

Owner publish (agents prep only): optional OS/2 **v2.3.0 @ `fc3ae21`**
(per #205); BIOS **v2.2.0** (fold capsule or **v2.2.1**); OS/2 **v2.3.1**
(or **v2.4.0** Gen2) after #184+#197; Soft = PLAY uploads, no suite tag.

Handshake: `-b55f` owns [os2#232](https://github.com/squirelworks/squirrel-os2/pull/232)
(`2.9-lm-kernel-tick`) then `2.9-syscall64` (coordinate on the agreed issue
before Soft touches that area). `-2eaf` Gen2 → W.6 after tip; Soft ISM apps
in parallel (soft #184–#190).

---

## Lane A checklist (`-2eaf` — this agent)

### squirrel-os2 — Generation 2 desktop (exclusive of 2.9)

- [x] **W.3-launchpad** `-2eaf` — PR #183 (merged)
- [ ] **2.6-icons** + **2.6-drag** `-2eaf` — PR #184
- [x] **2.6-shadows** `-2eaf` — PR #188 (merged)
- [x] **W.2-templates** `-2eaf` — PR #191 (merged)
- [x] **W.5-openas** `-2eaf` — PR #192 (merged)
- [ ] **Sarah Gen2 follow-ups** `-2eaf` — PR #197
- [ ] **W.6-startup** `-2eaf` — after icons/shadows on `main`
- [ ] **W.6-syssetup** `-2eaf` — same wave as W.6-startup

### squirrelsoft — GNSS CONTINUANCE

- [x] **Phone GnssMeasurement feed** `-2eaf` — PR #148 (on `main`)
- [x] **Live NTRIP transport** `-2eaf` — PR #150 (on `main`)
- [x] **RTCM decode skeleton** `-2eaf` — PR #151 / #152 (on `main`)
- [x] **RTCM Sarah round-2** `-2eaf` — PR #153 (merged)
- [x] **Post-#153 RTCM notes** `-2eaf` — PR #157 / #158 (on `main`)
- [x] **RTCM MSM observation fields** `-2eaf` — PR #160 (on `main`)
- [x] **Android NTRIP caster wiring** `-2eaf` — PR #162 (on `main`)

Leave **S6** / **PLAY** to Squirrel (`needs: squirrel`).

### squirrelbios

- [x] **UpdateCapsule + ESRT** `-2eaf` — PR #189 (merged)
- [ ] Free `cursor-codex` follow-ups not claimed by `-b55f` (capsule signed-apply, board configs, etc.)

### Hygiene

- [ ] Keep own open PRs mergeable on `main` and CI green
- [ ] Tag Lane A boxes with `lane \`-2eaf\`` in TODO/ROADMAP/HANDOFF in those PRs

---

## Lane B checklist (`-b55f` — other agent)

### squirrel-os2 — 64-bit kernel

- [x] **2.9-lm-paging** `-b55f` — PR #193 (merged)
- [x] **2.9-lm-stay-thunks** `-b55f` — PR #196 (merged)
- [x] **2.9-lm-boot64** `-b55f` — merged (follow-ups on `main`)
- [ ] **2.9-lm-kernel** tick `-b55f` — [os2#232](https://github.com/squirelworks/squirrel-os2/pull/232) (`REGRESS_FLAVOUR=lm64`, banner 2.9.0)
- [ ] **2.9-syscall64** `-b55f` — next after #232; Soft must coordinate before touching

**Stand down (`-2eaf`):** no competing OS/2 LM-kernel or syscall64 PRs.

---

## Open PR map (snapshot)

| Repo | PR | Branch | Tag | Box |
|------|----|--------|-----|-----|
| os2 | [#232](https://github.com/squirelworks/squirrel-os2/pull/232) | `…-2.9-lm-kernel-tick-b55f` | `-b55f` | 2.9-lm-kernel tick |
| soft | [#184](https://github.com/squirelworks/squirrelsoft/pull/184) | `…-ism-kbd-cfg-2eaf` | `-2eaf` | Eric keyboard cfg #180 |
| soft | [#186](https://github.com/squirelworks/squirrelsoft/pull/186) | `…-ism-mc-stubs-2eaf` | `-2eaf` | MC stubs ISM #81 |
| soft | [#187](https://github.com/squirelworks/squirrelsoft/pull/187) | `…-ism-hud-tool-2eaf` | `-2eaf` | HUD tool ISM #82 |
| soft | [#188](https://github.com/squirelworks/squirrelsoft/pull/188) | `…-ism-mouse-cfg-2eaf` | `-2eaf` | Eric mouse cfg #181 |
| soft | [#189](https://github.com/squirelworks/squirrelsoft/pull/189) | `…-ism-hil-bench-2eaf` | `-2eaf` | HIL UI ISM #80 |
| soft | [#190](https://github.com/squirelworks/squirrelsoft/pull/190) | `…-ism-bus-analyzer-2eaf` | `-2eaf` | bus analyzer ISM #83 *(merged)* |
| soft | [#205](https://github.com/squirelworks/squirrelsoft/pull/205) | `…-ism-ram-test-2eaf` | `-2eaf` | Eric RAM self-test #182 *(merged)* |
| soft | *(this PR)* | `…-ism-live-hid-2eaf` | `-2eaf` | Soft HID path S8.2 |

**Merged (do not re-claim):** os2 #183 W.3, #188 shadows, #191 W.2, #192 W.5,
#193 paging, #196 stay-thunks, #203 lanes canon; bios #189 capsule; soft
#148–#153 GNSS/NTRIP/RTCM (+ Sarah round-2), #157 / #158 post-#153 notes,
#160 MSM obs, #162 Android NTRIP, #165 PATH_TO_3.0.0 (suite index retarget
follows on `cursor/path-point-os2-2eaf`), Soft ISM #184–#190 (kbd/mouse/HIL/
MC/HUD/busmon).

## Issue sync note

**Canonical living claim list:** [squirrel-os2#201](https://github.com/squirelworks/squirrel-os2/issues/201).
Agent `gh` cannot edit/close issues (create-only); Sarah closes #198/#199/#200/#202
when convenient. This file mirrors #201 for Soft/suite scope.
