# Play-ready AABs (workspace downloads)

Upload-key–signed release App Bundles for the current Play-safe version bumps on
`cursor/triple-aab-bump-dc64`. Download these from the repo workspace (or this
PR) and upload in Play Console — nothing is pushed to Play from the agent.

| File | Package | versionName | versionCode |
|------|---------|-------------|-------------|
| `SquirrelSuica-0.0.8.aab` | `works.squirrel.suica` | 0.0.8 | 8 |
| `SquirrelSound-0.0.7.aab` | `works.squirrel.sound` | 0.0.7 | 7 |
| `SquirrelQR-0.1.5.aab` | `works.squirrel.qr` | 0.1.5 | 8 |

Absolute paths on the cloud agent VM:

- `/agent/repos/squirrelsoft/workspaces/SquirrelSuica-0.0.8.aab`
- `/agent/repos/squirrelsoft/workspaces/SquirrelSound-0.0.7.aab`
- `/agent/repos/squirrelsoft/workspaces/SquirrelQR-0.1.5.aab`

Copies also under `/opt/cursor/artifacts/` with the same filenames.
SHA-256: `SHA256SUMS.txt` in this directory.

Signed with the squirrel upload keystore (`CN=Squirrel Works Upload`). Register
that upload certificate in Play App Signing if this is the first upload key for
the app.
