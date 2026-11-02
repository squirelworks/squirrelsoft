# Play-ready AABs (local / artifacts only — NOT in git)

Signed release App Bundles are **not committed to GitHub**. Build and sign
locally, or use copies left on the cloud agent VM under:

- `/opt/cursor/artifacts/`
- `/agent/repos/squirrelsoft/workspaces/` (untracked; gitignored)
- `/agent/repos/squirrelsoft/.local-aabs/` (untracked; gitignored)

Do **not** force-add `*.aab` files. Root `.gitignore` ignores `*.aab` and
`workspaces/*.aab` with no exceptions.

## SquirrelSound 0.0.8 (release staging branch)

| File | Package | versionName | versionCode |
|------|---------|-------------|-------------|
| `SquirrelSound-0.0.8.aab` | `works.squirrel.sound` | 0.0.8 | 8 |

Expected local paths:

- `/opt/cursor/artifacts/SquirrelSound-0.0.8.aab`
- `/agent/repos/squirrelsoft/workspaces/SquirrelSound-0.0.8.aab` (if present locally)
- `/agent/repos/squirrelsoft/.local-aabs/SquirrelSound-0.0.8.aab`

SHA-256 reference: `SHA256SUMS.txt` in this directory (checksums of local/artifact
files only — the binaries are not in the repository).

Signed with the squirrel upload keystore (`CN=Squirrel Works Upload`). Play
already has versionCode 7 for this package — upload **8** next.

## Play Console upload

1. Open [Play Console](https://play.google.com/console) → **SquirrelSound**
   (`works.squirrel.sound`).
2. **Testing → Internal testing** (or closed/open/production as needed) →
   **Create new release**.
3. Upload the local/artifact `SquirrelSound-0.0.8.aab` (not a GitHub raw URL).
4. Add release notes (spectrum Basic/Advanced UX, range, zoom/scroll), review,
   roll out.
