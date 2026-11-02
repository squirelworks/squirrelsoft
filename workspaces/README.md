# Play-ready AABs (local / artifacts only — NOT in git)

Signed release App Bundles are **not committed to GitHub**. Build and sign
locally, or use copies left on the cloud agent VM under:

- `/opt/cursor/artifacts/`
- `/agent/repos/squirrelsoft/workspaces/` (untracked; gitignored)
- `/agent/repos/squirrelsoft/.local-aabs/` (untracked; gitignored)

Do **not** force-add `*.aab` files. Root `.gitignore` ignores `*.aab` and
`workspaces/*.aab` with no exceptions.

## Version bumps on this branch

| File | Package | versionName | versionCode |
|------|---------|-------------|-------------|
| `SquirrelSuica-0.0.8.aab` | `works.squirrel.suica` | 0.0.8 | 8 |
| `SquirrelSound-0.0.7.aab` | `works.squirrel.sound` | 0.0.7 | 7 |
| `SquirrelQR-0.1.5.aab` | `works.squirrel.qr` | 0.1.5 | 8 |

Expected local paths (same filenames under each root):

- `/opt/cursor/artifacts/`
- `/agent/repos/squirrelsoft/workspaces/` (if present locally)
- `/agent/repos/squirrelsoft/.local-aabs/`

SHA-256 reference: `SHA256SUMS.txt` in this directory (checksums of local/artifact
files only — the binaries are not in the repository).

Signed with the squirrel upload keystore (`CN=Squirrel Works Upload`). Register
that upload certificate in Play App Signing if this is the first upload key for
the app.

## Play Console upload

1. Open [Play Console](https://play.google.com/console) for each app.
2. **Testing → Internal testing** (or closed/open/production as needed) →
   **Create new release**.
3. Upload the matching local/artifact `.aab` (not a GitHub raw URL).
4. Review and roll out.
