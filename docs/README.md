# Store / privacy docs

Public HTML privacy policies for Google Play listings.

Play requires a **login-free HTML** page. GitHub blob URLs are not enough.
jsDelivr works without GitHub Pages. Use the **`main`** tip (stable while files remain on `main`):

| App | File | Public URL |
|-----|------|------------|
| Suica Reader (`works.squirrel.suica`) | `privacy-suica.html` | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-suica.html |
| SquirrelSound (`works.squirrel.sound`) | `privacy-sound.html` | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-sound.html |
| SquirrelQR (`works.squirrel.qr`) | `privacy-qr.html` | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-qr.html |

Paste each URL in Play Console → **Policy and programs → App content → Privacy policy**.
The Android Publisher API cannot set the privacy-policy field for Play-hosted apps.

**Full App content answers** (privacy, sign-in, ads, content rating, target audience,
data safety, government, financial features, health): see
[`play-app-content-checklist.md`](play-app-content-checklist.md).

**Contact website** (store listing / `edits.details`) should be the repo URL
`https://github.com/squirelworks/squirrelsoft`, not the privacy HTML URL.
`scripts/play_formup.py` sets contact email + website via API; privacy URLs are printed for Console paste.

## Play store graphics

Store icons and feature graphics live in `docs/play-graphics/` so listing scripts work
outside the author's machine:

| File pattern | Size |
|--------------|------|
| `{suica,sound,qr}-icon-512.png` | 512×512 |
| `{suica,sound,qr}-feature-1024x500.png` | 1024×500 |
| `{suica,sound,qr}-phone-*.png` (optional) | phone screenshots |

Override with `--graphics-dir` or `PLAY_GRAPHICS_DIR`. Fallback search order when unset:
`docs/play-graphics/`, then `/opt/cursor/artifacts/play-graphics/`. Missing dirs fail loudly.
