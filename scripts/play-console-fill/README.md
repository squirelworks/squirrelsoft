# Play Console local filler (Playwright + Snap Brave CDP)

Fills **Policy → App content** for:

| Package | Key |
|---------|-----|
| `works.squirrel.suica` | `suica` |
| `works.squirrel.sound` | `sound` |
| `works.squirrel.qr` | `qr` |

Connects to your already-running **Brave** via `connectOverCDP('http://127.0.0.1:9222')`.  
Does **not** launch a headless browser or touch the cloud VM desktop.

Answers match `docs/play-app-content-checklist.md` (privacy jsDelivr `@main` URLs, sign-in none, ads no, IARC best-effort, audience 18+, data safety no collection, government no, financial none, health none, internal tester `squirelworks@gmail.com`).

---

## 1. Start Snap Brave with remote debugging

**Quit Brave completely** (all windows), then paste:

```bash
/snap/bin/brave \
  --remote-debugging-port=9222 \
  --user-data-dir=$HOME/snap/brave/current/.config/BraveSoftware/Brave-Browser
```

**If that profile path is missing**, try the Snap `common` path:

```bash
/snap/bin/brave \
  --remote-debugging-port=9222 \
  --user-data-dir=$HOME/snap/brave/common/.config/BraveSoftware/Brave-Browser
```

Confirm CDP is up:

```bash
curl -s http://127.0.0.1:9222/json/version | head
```

In that Brave window:

1. Open [https://play.google.com/console](https://play.google.com/console)
2. Sign in as the Play owner (`squirelworks@gmail.com`)
3. Leave the Console tab open

---

## 2. Install & run (from this folder)

Copy this directory anywhere, e.g. `~/play-console-fill`, or use it in-repo:

```bash
# Option A — in-repo
cd /path/to/squirrelsoft/scripts/play-console-fill

# Option B — copied home dir
# cp -a scripts/play-console-fill ~/play-console-fill && cd ~/play-console-fill

npm install
```

**Probe** (checks CDP + login, writes `out/probe.png`):

```bash
npm run probe
```

**One app at a time** (recommended):

```bash
npm run fill:suica
npm run fill:sound
npm run fill:qr
```

**All three** (still sequential, one app fully then the next):

```bash
npm run fill
```

**Subset of cards**:

```bash
npm run fill -- --app suica --cards privacy,ads,signin,government
```

**Custom CDP URL**:

```bash
PLAY_CDP_URL=http://127.0.0.1:9222 npm run fill:suica
# or
npm run fill -- --cdp http://127.0.0.1:9222 --app qr
```

---

## 3. Behavior

- **Anti-loop:** each card attempted at most twice (`--max-attempts 2`).
- **Screenshots:** `./out/<app>_<card>.png`; failures use `*_FAIL.png` / `*_ERR.png`.
- **Logs:** `./out/fill.log` + `./out/status.json` with per-card `done` / `manual` / `skipped` / `error`.
- If a card’s DOM cannot be automated, the script prints an **exact MANUAL** click line for that card only and continues.
- IARC questionnaire selectors vary; the script is best-effort and logs clearly when UI differs — verify Content ratings in Console after the run.
- Does **not** close your Brave session.

---

## 4. Cards covered

| Flag | Console item |
|------|----------------|
| `privacy` | Privacy policy (jsDelivr `@main` URL) |
| `signin` | App access — no restricted parts |
| `ads` | Ads — No |
| `adid` | Advertising ID — No (skipped if absent) |
| `iarc` | Content ratings questionnaire |
| `audience` | Target audience — 18+, not for children |
| `datasafety` | Data safety — no collection (confirm Submit) |
| `government` | Government apps — No |
| `financial` | Financial features — None |
| `health` | Health — none |
| `testers` | Internal testing email list (`squirelworks@gmail.com`) |

---

## 5. Troubleshooting

| Symptom | Fix |
|---------|-----|
| `CDP connect failed` | Brave not started with `--remote-debugging-port=9222`, or another Chrome already owns 9222 |
| `LOGIN required` | Sign into Play Console in the debug Brave profile, re-run `npm run probe` |
| Wrong / empty profile | Use the `current` path first; fall back to `common` (see §1) |
| Card stuck | Open `out/*_FAIL.png`, follow the printed `MANUAL:` line, re-run with `--cards thatcard` |

Developer / app IDs live in `config.mjs` (`PLAY_DEV_ID` override if your Console URL differs).
