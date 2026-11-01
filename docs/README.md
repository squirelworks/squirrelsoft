# Store / privacy docs

Public HTML privacy policies for Google Play listings.

Play requires a **login-free HTML** page. GitHub blob URLs are not enough.
jsDelivr works without GitHub Pages. Branch tip and pinned commit both return HTTP 200:

| App | File | Public URL |
|-----|------|------------|
| Suica Reader (`works.squirrel.suica`) | `privacy-suica.html` | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@cursor/play-console-checklist-dc64/docs/privacy-suica.html |
| SquirrelSound (`works.squirrel.sound`) | `privacy-sound.html` | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@cursor/play-console-checklist-dc64/docs/privacy-sound.html |
| SquirrelQR (`works.squirrel.qr`) | `privacy-qr.html` | https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@cursor/play-console-checklist-dc64/docs/privacy-qr.html |

Pinned-commit mirrors (stable until rewritten): replace the ref with `3481e7958904fc97b925f59cebd36d2fb7e5e649`.

Paste each URL in Play Console → **Policy and programs → App content → Privacy policy**.
The Android Publisher API cannot set the privacy-policy field for Play-hosted apps.
