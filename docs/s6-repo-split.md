# S6 — per-app repo split (prep inventory)

Roadmap [S6](../ROADMAP.md#s6--repo-split-32) / issue #32 / HANDOFF card **S6**.
**Squirrel** creates the GitHub remotes and runs `git filter-repo`. Agents only
prepare the tree. This document and `scripts/s6_repo_split_prep.sh` are that
prep. Nothing here creates or deletes a repository.

Generate the same path lists from the script:

```sh
scripts/s6_repo_split_prep.sh            # inventory
scripts/s6_repo_split_prep.sh --check    # paths exist on disk
scripts/s6_repo_split_prep.sh --filter-repo
```

Suggested CI files to copy into each new repo live under
[`docs/s6-ci-stubs/`](s6-ci-stubs/). They are not wired into this umbrella's
`.github/workflows/`.

## Target repositories

Names match `ROADMAP.md` S6, plus `squirrelsoft-suica-analysis` (own Play app
and CI job today).

| Future repo | App folders | Core libs to carry (vendor or submodule) | Android `applicationId` |
|-------------|-------------|------------------------------------------|-------------------------|
| `squirrelsoft-suica` | `squirrelsuica/` | `libs/sqfelica` | `works.squirrel.suica` |
| `squirrelsoft-suica-analysis` | `squirrelsuica-analysis/` | `libs/sqfelica` | `works.squirrel.suicaanalysis` |
| `squirrelsoft-soundanalysis` | `squirrelsound/` | `libs/sqdsp`, `libs/sqgui` | `works.squirrel.sound` |
| `squirrelsoft-qr` | `squirrelqr/` | `libs/sqqr`, `libs/sqgui` | `works.squirrel.qr` |
| `squirrelsoft-cad` | `squirrelstation/`, `squirrelcad/` | `libs/sqgui` | — (desktop) |
| `squirrelsoft-gnss` | `squirrelgnss/` | — | — (desktop) |
| `squirrelsoft-calc` | `squirrelcalc/` | `libs/sqmath`, `libs/sqgui` | — (desktop) |
| `squirrelsoft-games` | `squirrelgames/` | `libs/sqgui` | — (desktop) |

`applicationId` and Play `versionCode` history must not change when the code
moves. Play uploads stay with Squirrel; AABs and keystores never enter any repo.

## Path lists (filter-repo)

Paths are relative to the umbrella root. Run filter-repo on a **fresh mirror
clone**, never against the live `origin/main` working tree.

### `squirrelsoft-suica`

- `squirrelsuica/` (Python tools, `data/`, `desktop/`, `android/`, tests, fixtures)
- `libs/sqfelica/`

```text
git filter-repo --path squirrelsuica --path libs/sqfelica
```

### `squirrelsoft-suica-analysis`

- `squirrelsuica-analysis/` (Python analyzer, `schema/`, `android/`, tests)
- `libs/sqfelica/`

```text
git filter-repo --path squirrelsuica-analysis --path libs/sqfelica
```

**Split blocker:** `suica_analyze.py` still loads
`../squirrelsuica/felica_parse.py` for history formatting. Before or at split,
either (a) vendor that module, (b) submodule `squirrelsoft-suica`, or (c) rely
only on `libs/sqfelica` + schema paths so the cross-app import can go.

### `squirrelsoft-soundanalysis`

- `squirrelsound/` (desktop C, Android, JNI smoke, docs)
- `libs/sqdsp/`
- `libs/sqgui/`

```text
git filter-repo --path squirrelsound --path libs/sqdsp --path libs/sqgui
```

### `squirrelsoft-qr`

- `squirrelqr/` (Android, desktop, web-demo, tests)
- `libs/sqqr/`
- `libs/sqgui/`

```text
git filter-repo --path squirrelqr --path libs/sqqr --path libs/sqgui
```

### `squirrelsoft-cad` (Station + CAD)

- `squirrelstation/`
- `squirrelcad/`
- `libs/sqgui/`

```text
git filter-repo --path squirrelstation --path squirrelcad --path libs/sqgui
```

### `squirrelsoft-gnss`

- `squirrelgnss/`

```text
git filter-repo --path squirrelgnss
```

### `squirrelsoft-calc`

- `squirrelcalc/`
- `libs/sqmath/`
- `libs/sqgui/`

```text
git filter-repo --path squirrelcalc --path libs/sqmath --path libs/sqgui
```

### `squirrelsoft-games`

- `squirrelgames/`
- `libs/sqgui/`

```text
git filter-repo --path squirrelgames --path libs/sqgui
```

## Umbrella (`squirrelsoft`) after the split

Keeps shared cores (canonical copy), Play helpers, suite docs, and the desktop
tools not listed in S6:

| Keep | Why |
|------|-----|
| `libs/` | Canonical shared cores; consumers vendor/submodule |
| `scripts/` | Play listing/upload helpers (`play_*.py`, `release/`) |
| `docs/` | Suite docs, privacy pages, release notes, this prep |
| `.github/workflows/` | Narrowed to umbrella `make test` + remaining apps |
| `squirrelpaint/`, `squirreldebug/` | Not in the S6 move list |
| `workspaces/`, top-level Makefile/README/ROADMAP/HANDOFF/LICENSE | Suite entry |

After each app leaves, drop its matrix entry from `.github/workflows/ci.yml`
and its choice from `release.yml` / `scripts/release/app_meta.sh`.

## Suggested CI per future repo

Stub workflows: [`docs/s6-ci-stubs/`](s6-ci-stubs/). Pattern matches today's
umbrella jobs, scoped to one repo:

| Repo | Host job | Android job |
|------|----------|-------------|
| `squirrelsoft-suica` | `make -C libs/sqfelica test` + `make -C squirrelsuica test` (PC/SC optional) | `./gradlew` in `squirrelsuica/android` |
| `squirrelsoft-suica-analysis` | `make -C squirrelsuica-analysis test` (+ JNI smoke) | `./gradlew` in `squirrelsuica-analysis/android` |
| `squirrelsoft-soundanalysis` | `make -C squirrelsound test` (SDL2/ALSA/Pulse) | `./gradlew` in `squirrelsound/android` |
| `squirrelsoft-qr` | `make -C squirrelqr test` (SDL2/X11) | `./gradlew` in `squirrelqr/android` |
| `squirrelsoft-cad` | `make -C squirrelstation test` + `make -C squirrelcad test` | — |
| `squirrelsoft-gnss` | `make -C squirrelgnss test` | — |
| `squirrelsoft-calc` | `make -C squirrelcalc test` | — |
| `squirrelsoft-games` | `make -C squirrelgames test` | — |

Rules that stay true in every stub: debug builds only; no AABs; no signing
secrets; synthetic Suica fixtures only.

## Umbrella README — suite links (draft)

Paste into `README.md` once remotes exist (URLs are placeholders until Squirrel
creates them):

```markdown
## Suite repositories

This umbrella holds shared cores (`libs/`), Play scripts, and desktop tools
that have not moved yet. Per-app repositories (history via `git filter-repo`):

| Repository | Contents |
|------------|----------|
| [squirrelsoft-suica](https://github.com/squirelworks/squirrelsoft-suica) | SquirrelSuica (desktop + Android) + `sqfelica` |
| [squirrelsoft-suica-analysis](https://github.com/squirelworks/squirrelsoft-suica-analysis) | SquirrelSuicaAnalysis + `sqfelica` |
| [squirrelsoft-soundanalysis](https://github.com/squirelworks/squirrelsoft-soundanalysis) | SquirrelSound + `sqdsp` / `sqgui` |
| [squirrelsoft-qr](https://github.com/squirelworks/squirrelsoft-qr) | SquirrelQR + `sqqr` / `sqgui` |
| [squirrelsoft-cad](https://github.com/squirelworks/squirrelsoft-cad) | SquirrelStation + SquirrelCAD + `sqgui` |
| [squirrelsoft-gnss](https://github.com/squirelworks/squirrelsoft-gnss) | SquirrelGNSS |
| [squirrelsoft-calc](https://github.com/squirelworks/squirrelsoft-calc) | SquirrelCalc + `sqmath` / `sqgui` |
| [squirrelsoft-games](https://github.com/squirelworks/squirrelsoft-games) | SquirrelGames + `sqgui` |

Related suite repos: [squirrelbios](https://github.com/squirelworks/squirrelbios),
[squirrel-os2](https://github.com/squirelworks/squirrel-os2).
```

## Owner checklist (Squirrel)

1. Create empty remotes with the names above (no agent creates or deletes them).
2. For each target: `git clone --mirror` this repo → `git filter-repo` with the
   paths in this doc → add the new `origin` → push.
3. Decide lib strategy per consumer: git submodule of umbrella `libs/<name>`,
   or a vendored copy updated by hand.
4. Resolve the SuicaAnalysis → `felica_parse.py` cross-import before or during
   that split.
5. Point each new repo's default branch CI at the matching stub under
   `docs/s6-ci-stubs/`.
6. Update this umbrella's README with the suite-links section once URLs are live.
7. Narrow umbrella CI/release matrices; leave Play scripts here.

## Split readiness (from `docs/architecture.md`)

An app is ready when: `make -C <app> test` (and Android `./gradlew` if any)
passes alone; it includes nothing from other app folders except `libs/`; it has
its own CI; cores are listed for vendor/submodule; Play `applicationId` /
`versionCode` history is recorded.
