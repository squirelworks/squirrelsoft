# Store / privacy docs

Public HTML privacy policies for Google Play listings:

| App | File | Suggested public URL (after push) |
|-----|------|-----------------------------------|
| Suica Reader (`works.squirrel.suica`) | `privacy-suica.html` | `https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@BRANCH/docs/privacy-suica.html` |
| SquirrelSound (`works.squirrel.sound`) | `privacy-sound.html` | `https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@BRANCH/docs/privacy-sound.html` |
| SquirrelQR (`works.squirrel.qr`) | `privacy-qr.html` | `https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@BRANCH/docs/privacy-qr.html` |

Play requires a **login-free HTML** page. GitHub blob URLs are not enough. Prefer jsDelivr (above) or enable GitHub Pages for this repo.

Paste each URL in Play Console → **Policy and programs → App content → Privacy policy**.
The Android Publisher API cannot set the privacy-policy field for Play-hosted apps.
