# Play Console store graphics

Required by `scripts/play_formup.py` and `scripts/play_listing.py`.

| File | Purpose |
|------|---------|
| `suica-icon-512.png` / `sound-icon-512.png` / `qr-icon-512.png` | Hi-res icon (512×512) |
| `suica-feature-1024x500.png` / `sound-…` / `qr-…` | Feature graphic |
| `*-phone-*.png` (optional) | Phone screenshots for formup |

Default path: this directory. Override with `--graphics-dir` or env `PLAY_GRAPHICS_DIR`.
Cloud agents may also use `/opt/cursor/artifacts/play-graphics/` as a fallback.
