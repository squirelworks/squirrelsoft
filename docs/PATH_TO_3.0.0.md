# Suite path to 3.0.0 (fine-grained tags)

**Living walkthrough** for finishing open work, cutting near-term releases, then
climbing to suite / OS **3.0.0** ("64-bit Warp" = CONTINUANCE **M3**). Written
2026-11-15 Asia/Tokyo for lanes **`-2eaf`** and **`-b55f`**.

| Pointer | Where |
|---------|--------|
| Suite plan (M1–M5) | [CONTINUANCE.md](CONTINUANCE.md) |
| Lane claims | [AGENT_LANES.md](AGENT_LANES.md), [os2#201](https://github.com/squirelworks/squirrel-os2/issues/201), Soft [#161](https://github.com/squirelworks/squirrelsoft/issues/161) |
| OS/2 ROADMAP 2.5–3.0 | [squirrel-os2/ROADMAP.md](https://github.com/squirelworks/squirrel-os2/blob/main/ROADMAP.md) |
| Release go/no-go (near-term) | Soft [#161](https://github.com/squirelworks/squirrelsoft/issues/161); OS/2 [#205](https://github.com/squirelworks/squirrel-os2/pull/205) |

Agents **never tag or publish**. Owner tags from prep notes. Prefer **many
small tags** (2.3.1, 2.3.2, 2.4.0…) over big jumps.

---

## 1. Principles

1. **Owner tags / Play uploads.** Agents write `documentation/releases/*.md`
   or `release-notes/*.md`, tick boxes, keep CI green. Never `git tag`, never
   Publish release, never upload AABs.
2. **Lanes.** `-2eaf` = Gen2 desktop (W.* / 2.6), Soft GNSS, free BIOS Phase G.
   `-b55f` = OS/2 **2.9-lm-*** → **2.9-syscall64** only. Canonical claim list:
   [os2#201](https://github.com/squirelworks/squirrel-os2/issues/201).
3. **No stacking.** Every PR bases on `main`. Rebase after the other lane
   merges; do not stack feature branches.
4. **Original code only.** Specs fine; no SeaBIOS/EDK2/osFree/Linux/Wine/… sources.
5. **Every behaviour change** carries: a harness check, a design note, boxes
   ticked in the same PR, and (when cutting a tag) a release-note file ready
   for the owner.
6. **Handshake (amended joint plan, 2026-11-15):**
   - `-b55f`: finish **#204** (rebase until green) → tick **2.9-lm-kernel** →
     **2.9-syscall64**.
   - `-2eaf`: finish **#184** → **#197** → **W.6-startup** / **W.6-syssetup**.
   - Soft **#162** (Android NTRIP caster) is on Soft `main` — PLAY uploads
     can proceed.
7. **#205 conflict resolved here:** publish optional historical OS/2
   **v2.3.0 @ `fc3ae21`** so notes match that tree; **no mid-flight Gen2/LM
   freeze**. After open feature PRs land, cut a **current-main** tag
   (**v2.3.1** or refreshed **v2.4.0** Gen2). Both are owner actions; agents
   only prep notes.

---

## 2. Near-term: finish open PRs, then release

Snapshot Asia/Tokyo 2026-11-15 (re-check `gh pr list` on resume).

### 2.1 Finish list (land first)

| Repo | PR | Lane | Boxes / topic | Gate |
|------|----|------|---------------|------|
| squirrel-os2 | [#184](https://github.com/squirelworks/squirrel-os2/pull/184) | `-2eaf` | **2.6-icons**, **2.6-drag** (`dk-icons`, `dk-drag`) | `tools/regress.sh` green + mergeable |
| squirrel-os2 | [#197](https://github.com/squirelworks/squirrel-os2/pull/197) | `-2eaf` | Sarah Gen2 follow-ups (context / captions / `wg_cur`) | same |
| squirrel-os2 | [#204](https://github.com/squirelworks/squirrel-os2/pull/204) | `-b55f` | **2.9-lm-boot64** (`LM_BOOT64` / everyday stay) | rebase off CONFLICTING → green |
| squirrel-os2 | [#205](https://github.com/squirelworks/squirrel-os2/pull/205) | `-b55f` | Docs: release coord (this plan's near-term half) | docs-only |
| squirrelsoft | [#162](https://github.com/squirelworks/squirrelsoft/pull/162) | `-2eaf` | Android NTRIP caster wiring | **MERGED** on `main` |
| squirrelsoft | [#165](https://github.com/squirelworks/squirrelsoft/pull/165) | `-2eaf` | docs `PATH_TO_3.0.0.md` | this PR |
| squirrelbios | [#189](https://github.com/squirelworks/squirrelbios/pull/189) | `-2eaf` | UpdateCapsule + ESRT | **MERGED** on `main` |

**Already on OS/2 `main` (do not re-claim):** W.3 #183, W.2 #191, W.5 #192,
2.6-shadows #188, 2.9 IDT #189, paging #193, stay-thunks #196.

### 2.2 Owner publish checklist (agents prep only)

| Order | Repo | Tag | Commit / notes | Depends on open PRs? |
|-------|------|-----|----------------|----------------------|
| 0 *(optional historical)* | squirrel-os2 | **v2.3.0** | **`fc3ae21`** (merge of #173); notes `documentation/releases/2.3.0.md` already match that tree (Gen2 "not started") | No — per [#205](https://github.com/squirelworks/squirrel-os2/pull/205) |
| 1 | squirrelbios | **v2.2.0** | Notes `release-notes/v2.2.0.md` on `main`. Capsule #189 is on `main`: **prefer refresh notes to mention capsule before tag**; if tagged without it, cut **v2.2.1** for capsule (`release-notes/capsule-esrt.md`, check `x86_64-capsule`) | #189 done |
| 2 | squirrel-os2 | **v2.3.1** *(preferred)* or **v2.4.0** Gen2 refresh | Current `main` after **#184 + #197** (+ whatever LM is on `main`; **#204 not required** for this cut). New notes file (see ladder). | #184, #197 |
| 3 | squirrelsoft | *no suite tag* | Owner **PLAY** uploads: Sound **0.0.14** / **0.0.15**, QR **0.1.7**, Suica **0.0.13** / **0.0.14** (already on Soft `main`). Soft **#162** is on `main`. | #162 done |

Compatibility matrix blurb (paste into each release note): OS/2 boots on
BIOS by BIOS / CSM / UEFI path; cite the BIOS tag and the OS/2
`uefi-squirrelbios` check when reliable.

---

## 3. Compatibility matrix (proposed)

| OS/2 tag | BIOS tag | Soft (apps) | Boot paths expected |
|----------|----------|-------------|---------------------|
| 2.3.0 @ `fc3ae21` | ≥ 2.1.1 (2.2.0 OK) | Play closed testing as of 2026-11-13 | BIOS + CSM; UEFI via OVMF/SquirrelBIOS |
| 2.3.1 / 2.4.0 Gen2 | **2.2.0** (+ capsule in-tree or **2.2.1**) | PLAY uploads + Soft #162 | same + note Gen2 desktop |
| 2.5.x (lm-kernel) | 2.2.x | Soft GNSS desktop/Android current | Everyday LM stay under `qemu-system-x86_64` |
| 2.6.x (syscall64) | 2.3.x (measured / SB follow-ups) | Soft cores linkable later | ELF64 `hello64` |
| **3.0.0** | BIOS with Secure Boot + project keys + TPM measure | Soft **1.0** track (M4, parallel) | Native 64-bit Warp on `x86_64-uefi` |

---

## 4. Version ladder — squirrel-os2 → 3.0.0

CONTINUANCE **M2** = Warp 4 generation (2.5–2.7 + W.2–W.6). **M3** = 64-bit
Warp (2.8-pae → 2.9 → accounts… → **3.0.0**). ROADMAP sections 2.5–3.0 name
the *feature* generations; **tags** below are the ship markers (many small).

| Tag | Boxes (TODO ids) | Lane | Acceptance checks | Release note to prep |
|-----|------------------|------|-------------------|----------------------|
| **2.3.0** | Historical Gen1 cut (2.2 + 2.3 + parallel tracks at prep) | owner | suite as of `fc3ae21` | *existing* `documentation/releases/2.3.0.md` |
| **2.3.1** | **2.6-icons**, **2.6-drag**, Sarah Gen2 (#197); plus Gen2 already on main (2.5-*, 2.6-context/notebook/shadows, W.2/W.3/W.4/W.5) and LM slices on main (IDT/paging/stay-thunks) | `-2eaf` finish #184/#197; owner tags | `dk-icons`, `dk-drag`, + existing `dk-shadow`, `wclp`, `openas`, `lm-stay`, `lm-page`, … | `documentation/releases/2.3.1.md` |
| **2.3.2** | **W.6-startup** | `-2eaf` | `w6-startup` → `startup: PASS` | `documentation/releases/2.3.2.md` |
| **2.4.0** | **W.6-syssetup** → **Gen2-complete** (M2 desktop done) | `-2eaf` | `w6-setup` → `syssetup: PASS` | `documentation/releases/2.4.0.md` |
| **2.4.1** | Optional polish / foreign-window leftovers if any open; else skip | open / `needs: squirrel` if docs only | named check(s) in that PR | `documentation/releases/2.4.1.md` |
| **2.5.0** | **2.9-lm-boot64** (#204) on `main` | `-b55f` | boot64 / `LM_BOOT64` checks from that PR | `documentation/releases/2.5.0.md` |
| **2.5.1** | **2.9-lm-kernel** *ticked* (stay + thunks + boot64 all on `main`) | `-b55f` | full `tools/regress.sh` under `qemu-system-x86_64` | `documentation/releases/2.5.1.md` |
| **2.6.0** | **2.9-syscall64** | `-b55f` | `hello64` | `documentation/releases/2.6.0.md` |
| **2.6.1** | CONTINUANCE **3a** recorded (freestanding C for new 2.9+ subsystems) + first C module if any | either / owner decides | docs + any new check | `documentation/releases/2.6.1.md` |
| **2.7.0** | **3.0-users** (login, account file, hashed passwords) | open (`cursor-codex` until claimed) | new `users-*` / login check (name when split) | `documentation/releases/2.7.0.md` |
| **2.7.1** | **3.0-perms** (owner/mode on SQFS) | open | new `perms-*` check | `documentation/releases/2.7.1.md` |
| **2.8.0** | **3.0-services** | open | new `svc-*` check | `documentation/releases/2.8.0.md` |
| **2.8.1** | **3.0-eventlog** | open | new `elog-*` check | `documentation/releases/2.8.1.md` |
| **2.9.0** | **3.0-taskmgr** | open | new `taskmgr-*` check | `documentation/releases/2.9.0.md` |
| **2.9.1** | **3.0-themes** (Warp 4 default + Classic + Luna-like) | open | new `theme-*` check | `documentation/releases/2.9.1.md` |
| **3.0.0** | M3 complete: 64-bit Warp desktop + accounts/services/themes; native boot on SquirrelBIOS `x86_64-uefi` with Secure Boot + project-signed loader measured into TPM | both lanes + BIOS companion | full regress + BIOS SB/TPM smoke cited in matrix | `documentation/releases/3.0.0.md` |

**Already done (do not retag for them alone):** **2.8-pae** (#180), 3.0
networking/sound half (`3.0-dhcp`…`3.0-media`), A.1–A.7 / C.* / G.*.

**Alt naming:** if the owner prefers a single Gen2 tag instead of 2.3.1, ship
**v2.4.0** as the post-#184/#197 cut and renumber W.6 to 2.4.1/2.4.2 — keep
the *boxes* the same.

---

## 5. Version ladder — squirrelbios (companion)

| Tag | Content | Lane | Acceptance | Notes file |
|-----|---------|------|------------|------------|
| **v2.2.0** | Everything prepared in `release-notes/v2.2.0.md` (D4, Phase E, F1–F4, Setup UI, host fuzz…). Fold capsule if not yet tagged | owner | `make test` (~337+) | `release-notes/v2.2.0.md` |
| **v2.2.1** | UpdateCapsule + ESRT if **not** folded into 2.2.0 | `-2eaf` (done on `main`) | `x86_64-capsule` | `release-notes/capsule-esrt.md` → promote to `v2.2.1.md` |
| **v2.2.2** | Capsule flash-rewrite / signed-apply follow-ups; Setup UI leftovers | `-2eaf` | named smokes | `release-notes/v2.2.2.md` |
| **v2.3.0** | Board configs (VisionFive 2 / HiFive Unmatched) and/or measured hand-off to OS | `-2eaf` / open | platform smokes | `release-notes/v2.3.0.md` |
| **v2.4.x…** | Secure Boot key story + TPM event log to OS (M3 firmware side) | open | SB + TPM smokes | per cut |
| **v3.x** | Align with OS/2 **3.0.0** verified chain | with OS/2 M3 | matrix row | with OS/2 3.0.0 notes |

C5 real-board flashing stays `needs: squirrel` (M5).

---

## 6. Version ladder — squirrelsoft (apps; no suite tag required)

| Mark | Content | Who | Gate |
|------|---------|-----|------|
| *coding done* | GNSS NTRIP/RTCM/MSM + Android caster (**#162**) | `-2eaf` | `make test` + Android tests |
| Sound **0.0.14** / **0.0.15** | PLAY closed testing upload | `needs: squirrel` | Play Console |
| QR **0.1.7** | PLAY upload | `needs: squirrel` | Play Console |
| Suica **0.0.13** / **0.0.14** | PLAY upload | `needs: squirrel` | Play Console |
| Desktop `VERSION` bumps | when behaviour ships (GNSS, etc.) | agent prep / owner draft Release | `docs/RELEASING.md` |
| Soft **1.0** (M4) | production Play + signed desktop pkgs + S6 split | mostly `needs: squirrel` | CONTINUANCE M4 |

Agents do **not** invent Play uploads. Prep notes under `docs/releases/`.

---

## 7. Major rungs (optional thin issues)

Prefer **one epic** + this doc over dozens of empty issues. If checklists grow,
open thin tracking issues titled:

| Rung | Theme |
|------|--------|
| 2.4 Gen2-complete | W.6 + 2.4.0 tag |
| 2.5 lm-kernel | #204 → 2.9-lm-kernel tick |
| 2.6 syscall64 | hello64 |
| 2.7 accounts… | 3.0-users → 3.0-themes ladder |
| 3.0.0 | M3 ship + BIOS companion |

Epic on Soft mirrors this file; OS/2 #201 stays the living *lane* list.

---

## 8. Resume when credits reset

1. Read this file + [AGENT_LANES.md](AGENT_LANES.md) + Soft [#161](https://github.com/squirelworks/squirrelsoft/issues/161) + [os2#201](https://github.com/squirelworks/squirrel-os2/issues/201).
2. `gh pr list --state open` in **squirrel-os2**, **squirrelbios**, **squirrelsoft**. Finish **your lane's** open PRs first (rebase, CI green, mergeable). Never redo a claimed box.
3. `-2eaf` next coding after #184/#197: **W.6-startup** then **W.6-syssetup** (new branches off latest `main`, suffix `-2eaf`). Soft: only hygiene / Sarah notes (#162 done).
4. `-b55f` next: rebase **#204** → tick **2.9-lm-kernel** when complete → **2.9-syscall64**.
5. After each mergeable wave: prep the next **release note** file from the ladder; ask owner to tag; update the compatibility matrix row.
6. Do not tag. Do not stack. Do not take the other lane's boxes.

---

## 9. CONTINUANCE map (cite)

| Milestone | This walkthrough |
|-----------|------------------|
| **M1** — Ship what `main` has | §2 near-term tags (BIOS 2.2.0, OS/2 2.3.0/2.3.1, Soft PLAY) |
| **M2** — Warp 4 generation | OS/2 tags through **2.4.0** Gen2-complete |
| **M3** — 64-bit Warp → **3.0.0** | OS/2 **2.5.0…2.9.1** then **3.0.0** + BIOS SB/TPM companion |
| **M4** — Soft 1.0 | §6 Soft ladder |
| **M5** — Real hardware | Post-merge notes; not CI gates |

OS/2 ROADMAP **2.5–2.7** = Gen2 feature boxes; **2.8–2.9** = 64-bit bridge;
**3.0** = accounts/services/themes (tag **3.0.0** when that generation ships
on the 64-bit kernel).
