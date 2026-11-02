# Play-ready AABs (workspace downloads)

Upload-key–signed release App Bundle for SquirrelSound with Basic/Advanced
spectrum UX (`cursor/sound-spectrum-ux-dc64` → release branch
`cursor/sound-aab-008-dc64`). Download from the repo workspace (or this PR) and
upload in Play Console — nothing is pushed to Play from the agent.

| File | Package | versionName | versionCode |
|------|---------|-------------|-------------|
| `SquirrelSound-0.0.8.aab` | `works.squirrel.sound` | 0.0.8 | 8 |

Absolute paths on the cloud agent VM:

- `/agent/repos/squirrelsoft/workspaces/SquirrelSound-0.0.8.aab`

Copy also under `/opt/cursor/artifacts/SquirrelSound-0.0.8.aab`.
SHA-256: `SHA256SUMS.txt` in this directory.

GitHub raw (after push):

- https://github.com/squirelworks/squirrelsoft/raw/cursor/sound-aab-008-dc64/workspaces/SquirrelSound-0.0.8.aab

Signed with the squirrel upload keystore (`CN=Squirrel Works Upload`). Play
already has versionCode 7 for this package — upload **8** next.

## Play Console upload

1. Open [Play Console](https://play.google.com/console) → **SquirrelSound**
   (`works.squirrel.sound`).
2. **Testing → Internal testing** (or closed/open/production as needed) →
   **Create new release**.
3. Upload `workspaces/SquirrelSound-0.0.8.aab`.
4. Add release notes (spectrum Basic/Advanced UX, range, zoom/scroll), review,
   roll out.
