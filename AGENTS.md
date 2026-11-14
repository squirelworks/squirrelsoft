# Agent guide (Cursor, Codex and other coding agents)

SquirrelSoft: Android apps (SquirrelSuica, SquirrelSuicaAnalysis, SquirrelSound, SquirrelQR) and desktop
C programs (Station, CAD, GNSS, Sound, Suica, QR, Calc, Paint, Games, Debug) on shared C cores in `libs/`.
It is also the umbrella for the suite: github.com/squirelworks/squirrelbios and squirrel-os2.

- **Start here:** [HANDOFF.md](HANDOFF.md) (done / in progress / open, with a card per open item),
  [ROADMAP.md](ROADMAP.md), and for the long view [docs/CONTINUANCE.md](docs/CONTINUANCE.md).
- **Build:** `make`. **Test:** `make test`; Android per app:
  `cd <app>/android && ./gradlew :app:assembleDebug :app:testDebugUnitTest`. CI runs both on every PR.
- **PRs:** base on `main`; never stack on another feature branch; group related steps into one
  sizeable PR; confirm the PR targets `main`, is mergeable and CI is green.
- **Real card data never leaves the machine.** No real Suica/PASMO/nanaco/WAON dumps, IDm/PMm,
  balances, history rows or station/terminal codes in commits, PRs, issues, comments, fixtures or logs.
  Use the synthetic fixtures (`squirrelsuica/tests/fixtures/demo-*.json`).
- No AABs, keystores, passwords or Play keys anywhere in the repo, PRs, artifacts or logs. The owner
  uploads to Play and publishes releases. Never delete branches.
- Original code only; each app is a portable core with tests plus thin native front-ends.
