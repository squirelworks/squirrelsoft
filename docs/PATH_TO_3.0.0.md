# Suite path to 3.0.0 (index)

**This file is a short suite index**, not the OS version ladder.

| Canonical | Where |
|-----------|--------|
| **OS/2 3.0.0 walkthrough** (fine-grain tags, dual-agent resume, tip-cut rules) | squirrel-os2 [`documentation/releases/TO_3.0.0.md`](https://github.com/squirelworks/squirrel-os2/blob/cursor/release-coord-b55f/documentation/releases/TO_3.0.0.md) via [os2#205](https://github.com/squirelworks/squirrel-os2/pull/205) (`cursor/release-coord-b55f`); on `main` when that PR merges |
| Suite milestones (M1–M5) | [CONTINUANCE.md](CONTINUANCE.md) |
| Lane claims | [AGENT_LANES.md](AGENT_LANES.md), [os2#201](https://github.com/squirelworks/squirrel-os2/issues/201), Soft [#161](https://github.com/squirelworks/squirrelsoft/issues/161) |
| Soft go/no-go | Soft [#161](https://github.com/squirelworks/squirrelsoft/issues/161) |

Do **not** reinvent the OS/2 ladder here. Soft hosts CONTINUANCE as the suite umbrella; the **OS** climb to **3.0.0** is owned by squirrel-os2.

Agents **never tag or publish**. Owner tags from prep notes. Prefer many small tags over big jumps.

---

## Soft near-term (this repo only)

| Item | Who | Status |
|------|-----|--------|
| Android NTRIP caster (#162) | `-2eaf` | **Done** on Soft `main` |
| PLAY uploads: Sound **0.0.14** / **0.0.15**, QR **0.1.7**, Suica **0.0.13** / **0.0.14** | `needs: squirrel` | Owner Play Console only |
| **S6** per-app repo split | `needs: squirrel` | Prep inventory on `main`; owner executes |
| Soft **1.0** (CONTINUANCE **M4**) | mostly `needs: squirrel` | Production Play + signed desktop pkgs + S6 |

Agents do not invent Play uploads. Prep notes under `docs/releases/` when asked. Soft coding on lane `-2eaf` is hygiene unless Sarah opens a Soft follow-up.

---

## BIOS companion (pointer only)

Soft does **not** own the BIOS version ladder. Near-term owner cut:

- **v2.2.0** — notes `release-notes/v2.2.0.md` on squirrelbios `main` (capsule/ESRT already on `main`; fold into 2.2.0 or cut **v2.2.1**)
- Later SB / TPM / board work follows BIOS ROADMAP / HANDOFF; cite BIOS tags in OS/2 release notes when cutting suite matrix rows

Detail and acceptance checks live in **squirrelbios**, not here. OS/2 `TO_3.0.0.md` cites BIOS companions where the OS tip cut needs them.

---

## CONTINUANCE map

| Milestone | Where the detail lives |
|-----------|------------------------|
| **M1** — Ship what `main` has | Owner: BIOS 2.2.0, OS/2 tip cut per os2 `TO_3.0.0.md`, Soft PLAY above |
| **M2** — Warp 4 generation | OS/2 Gen2 / W.* — os2 `TO_3.0.0.md` + ROADMAP |
| **M3** — 64-bit Warp → **3.0.0** | **Canonical:** os2 `documentation/releases/TO_3.0.0.md` |
| **M4** — Soft 1.0 | Soft near-term table + CONTINUANCE M4 |
| **M5** — Real hardware | CONTINUANCE M5; `needs: squirrel` |

Lane handshake and open-PR map: [AGENT_LANES.md](AGENT_LANES.md). Resume after credit reset: read os2 `TO_3.0.0.md` first for OS work; Soft agents only check this index + Soft HANDOFF.
