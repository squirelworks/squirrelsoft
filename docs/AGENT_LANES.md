# Suite agent lanes (2026-11-15 Asia/Tokyo)

Two Cursor agents are active on the Squirrel suite. Branch suffixes identify
ownership. **Do not take a box that the other lane has claimed.** Finish or
rebase an existing PR; never redo it on a new branch.

**Living OS/2 lane issue (canonical):** [squirrel-os2#201](https://github.com/squirelworks/squirrel-os2/issues/201)
(chosen over #200 by rock–paper–scissors; see [#202](https://github.com/squirelworks/squirrel-os2/issues/202)).
If this file and #201 disagree, prefer #201 for claim lines and `main` for code.

| Lane | Branch suffix | Scope |
|------|---------------|-------|
| **A** | `-2eaf` | Gen2 desktop finish; Soft GNSS/NTRIP/RTCM; BIOS leftover Phase G / board configs; CI/rebase hygiene on own PRs |
| **B** | `-b55f` | **2.9-lm-kernel** remainder + **2.9-syscall64** (and follow-ups that stay inside long-mode) |

Shared files (`TODO.md`, `ROADMAP.md`, `HANDOFF.md`, check lists): rebase onto
`main` after the other merges; never stack feature branches. Prefer one agent
editing the docs boxes for its own cards per PR.

---

## Lane A checklist (`-2eaf` — this agent)

### squirrel-os2 — Generation 2 desktop (exclusive of 2.9)

- [ ] **W.3-launchpad** `-2eaf` — PR #183
- [ ] **2.6-icons** + **2.6-drag** `-2eaf` — PR #184
- [ ] **2.6-shadows** `-2eaf` — PR #188
- [x] **W.2-templates** `-2eaf` — PR #191 (merged)
- [ ] **W.5-openas** `-2eaf` — PR #192
- [ ] **W.6-startup** `-2eaf` — after icons/shadows on `main`
- [ ] **W.6-syssetup** `-2eaf` — same wave as W.6-startup

### squirrelsoft — GNSS CONTINUANCE

- [x] **Phone GnssMeasurement feed** `-2eaf` — PR #148 (on `main`)
- [x] **Live NTRIP transport** `-2eaf` — PR #150 (on `main`)
- [x] **RTCM decode skeleton** `-2eaf` — PR #151 / #152 (on `main`)
- [x] **RTCM Sarah round-2** `-2eaf` — PR #153 (merged)

Remaining Soft coding is CI/rebase hygiene. Leave **S6** / **PLAY** to
Squirrel (`needs: squirrel`).

### squirrelbios

- [ ] Free `cursor-codex` follow-ups not claimed by `-b55f` (Setup UI next steps, etc.)

### Hygiene

- [ ] Keep own open PRs mergeable on `main` and CI green
- [ ] Tag Lane A boxes with `lane \`-2eaf\`` in TODO/ROADMAP/HANDOFF in those PRs

---

## Lane B checklist (`-b55f` — other agent)

### squirrel-os2 — 64-bit kernel

- [x] **2.9-lm-paging** `-b55f` — PR #193 (merged)
- [ ] **2.9-lm-stay-thunks** `-b55f` — PR #196
- [ ] **2.9-lm-boot64** `-b55f` — after #196
- [ ] **2.9-lm-kernel** tick `-b55f` — only when stay+thunks+boot64 are on `main`
- [ ] **2.9-syscall64** `-b55f` — after lm-kernel ticked

**Stand down from:** W.3, 2.6-*, W.2, W.5, W.6 while Lane A PRs are open.

---

## Open PR map (snapshot)

| Repo | PR | Branch | Tag | Box |
|------|----|--------|-----|-----|
| os2 | #183 | `…-w3-launchpad-2eaf` | `-2eaf` | W.3-launchpad |
| os2 | #184 | `…-2.6-icons-drag-2eaf` | `-2eaf` | 2.6-icons, 2.6-drag |
| os2 | #188 | `…-2.6-shadows-2eaf` | `-2eaf` | 2.6-shadows |
| os2 | #192 | `…-w5-openas-2eaf` | `-2eaf` | W.5-openas |
| os2 | #196 | `…-2.9-lm-stay-thunks-b55f` | `-b55f` | 2.9-lm-stay-thunks |

**Merged (do not re-claim):** os2 #191 W.2, #193 paging; soft #148–#153 GNSS/NTRIP/RTCM (+ Sarah round-2).

## Issue sync note

**Canonical living claim list:** [squirrel-os2#201](https://github.com/squirelworks/squirrel-os2/issues/201).
Agent `gh` cannot edit/close issues (create-only); Sarah closes #198/#199/#200/#202
when convenient. This file mirrors #201 for Soft/suite scope.
