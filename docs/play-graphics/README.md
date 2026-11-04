# Play Console store graphics

Required by `scripts/play_formup.py` and `scripts/play_listing.py`.

| File | Purpose |
|------|---------|
| `suica-icon-512.png` / `sound-…` / `qr-…` / `suicaanalysis-…` | Hi-res icon (512×512) |
| `suica-feature-1024x500.png` / `sound-…` / `qr-…` / `suicaanalysis-…` | Feature graphic |
| `*-phone-*.png` (optional) | Phone screenshots for formup |

Default path: this directory. Override with `--graphics-dir` or env `PLAY_GRAPHICS_DIR`.
Cloud agents may also use `/opt/cursor/artifacts/play-graphics/` as a fallback.

## Sources

`src/` holds sources for the SquirrelSuicaAnalysis graphics; the older three
apps' PNGs have no sources in the repo.

- `suicaanalysis-icon.svg`, `suicaanalysis-feature.svg`: same layout as the
  other apps (dark ring + mark; title and subtitle on the right), on the
  analysis app's navy with the Suica card and a magnifier.
- `suicaanalysis-launcher-foreground.svg`: the app's adaptive launcher icon
  (the same mark on `#1A2440`), so it no longer looks like Suica Reader's. The
  build script also writes the legacy square and round mipmaps.
- `suicaanalysis-phone-*.png`: the real `MainActivity` layout rendered at
  1080×1920 by Robolectric (`squirrelsuica-analysis/android/app/src/storeShots/`)
  with a synthetic card, so no real card data is in the screenshots. That
  source set and Robolectric are only added with `-PstoreShots`, so normal
  builds and CI don't use them.

Regenerate everything with `docs/play-graphics/src/build-suicaanalysis.sh`
(needs Node with Playwright and an Android SDK).
