# Suite agent lanes (2026-11-15 Asia/Tokyo)

Two Cursor agents are active on the Squirrel suite. Branch suffixes identify
ownership. **Do not take a box that the other lane has claimed.** Finish or
rebase an existing PR; never redo it on a new branch.

| Lane | Branch suffix | Scope |
|------|---------------|-------|
| **A** | `-2eaf` | Gen2 desktop finish; Soft GNSS/NTRIP/RTCM; BIOS leftover Phase G / board configs; CI/rebase hygiene on own PRs |
| **B** | `-b55f` | **2.9-lm-kernel** and **2.9-syscall64** only (and follow-ups that stay inside long-mode) |

Shared files (`TODO.md`, `ROADMAP.md`, `HANDOFF.md`, check lists): rebase onto
`main` after the other merges; never stack feature branches. Prefer one agent
editing the docs boxes for its own cards per PR.

---

## Lane A checklist (`-2eaf` — this agent)

### squirrel-os2 — Generation 2 desktop (exclusive of 2.9)

- [ ] **W.3-launchpad** `-2eaf` — PR #183
- [ ] **2.6-icons** + **2.6-drag** `-2eaf` — PR #184
- [ ] **2.6-shadows** `-2eaf` — PR #188
- [ ] **W.2-templates** `-2eaf` — PR #191
- [ ] **W.5-openas** `-2eaf` — PR #192
- [ ] **W.6-startup** `-2eaf` — after icons/shadows on `main`
- [ ] **W.6-syssetup** `-2eaf` — same wave as W.6-startup

### squirrelsoft — GNSS CONTINUANCE

- [x] **Phone GnssMeasurement feed** `-2eaf` — PR #148 (on `main`)
- [x] **Live NTRIP transport** `-2eaf` — PR #150 (on `main`)
- [x] **RTCM decode skeleton** `-2eaf` — branch `cursor/impl-gnss-rtcm-2eaf`

Leave **S6** / **PLAY** to Squirrel (`needs: squirrel`).

### squirrelbios

- [ ] Free `cursor-codex` follow-ups not claimed by `-b55f` (Setup UI next steps, etc.)

### Hygiene

- [ ] Keep own open PRs mergeable on `main` and CI green
- [ ] Tag Lane A boxes with `lane \`-2eaf\`` in TODO/ROADMAP/HANDOFF in those PRs

---

## Lane B checklist (`-b55f` — other agent)

### squirrel-os2 — 64-bit kernel

- [ ] **2.9-lm-kernel** `-b55f` — PR #193 and further slices until the TODO box ticks
- [ ] **2.9-syscall64** `-b55f` — after lm-kernel on `main`

**Stand down from:** W.3, 2.6-*, W.2, W.5, W.6 while Lane A PRs are open.

---

## Open PR map (snapshot)

| Repo | PR | Branch | Tag | Box |
|------|----|--------|-----|-----|
| os2 | #183 | `…-w3-launchpad-2eaf` | `-2eaf` | W.3-launchpad |
| os2 | #184 | `…-2.6-icons-drag-2eaf` | `-2eaf` | 2.6-icons, 2.6-drag |
| os2 | #188 | `…-2.6-shadows-2eaf` | `-2eaf` | 2.6-shadows |
| os2 | #191 | `…-w2-templates-2eaf` | `-2eaf` | W.2-templates |
| os2 | #192 | `…-w5-openas-2eaf` | `-2eaf` | W.5-openas |
| os2 | #193 | `…-2.9-lm-paging-b55f` | `-b55f` | 2.9-lm-kernel (partial) |
| soft | #148 | `…-gnss-android-meas-2eaf` | `-2eaf` | Phone GnssMeasurement (merged) |
| soft | #150 | `…-gnss-ntrip-live-2eaf` | `-2eaf` | Live NTRIP transport (merged) |
| soft | (open) | `…-gnss-rtcm-2eaf` | `-2eaf` | RTCM decode skeleton |

## Issue sync note

GitHub issue bodies (#132 OS/2, #82 BIOS, #131 Soft) are owner-edited; agent
`gh` is read-only. Treat this file + each repo `HANDOFF.md` “Agent lanes”
section as the live claim list; paste into #132 / #169 when convenient.
