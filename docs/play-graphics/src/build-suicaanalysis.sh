#!/usr/bin/env bash
# Regenerate the SquirrelSuicaAnalysis Play Store graphics in docs/play-graphics/.
#   icon + feature graphic: SVG in this folder, rendered by Chromium (render.js, needs playwright)
#   phone screenshots: the real MainActivity layout, rendered by Robolectric with a synthetic card
# Needs an Android SDK (local.properties or ANDROID_HOME) for the screenshots.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(cd "$here/.." && pwd)"
app="$(cd "$here/../../../squirrelsuica-analysis/android" && pwd)"

node "$here/render.js" \
  "$here/suicaanalysis-icon.svg" "$out/suicaanalysis-icon-512.png" \
  "$here/suicaanalysis-feature.svg" "$out/suicaanalysis-feature-1024x500.png"

rm -rf "$app/app/build/store-shots"
(cd "$app" && ./gradlew -q :app:testDebugUnitTest -PstoreShots --tests '*StoreScreenshots*' --rerun)
rm -f "$out"/suicaanalysis-phone-*.png
for f in "$app"/app/build/store-shots/*.png; do
  cp "$f" "$out/suicaanalysis-phone-$(basename "$f")"
done
ls -1 "$out"/suicaanalysis-*.png

# Launcher icon: adaptive foreground plus legacy square/round mipmaps.
res="$app/app/src/main/res"
node "$here/render.js" \
  "$here/suicaanalysis-launcher-foreground.svg" "$res/drawable/ic_launcher_foreground.png" \
  "$here/suicaanalysis-icon.svg" "$here/.launcher-full.png"
python3 - "$here/.launcher-full.png" "$res" <<'PY'
import sys
from PIL import Image, ImageDraw
full = Image.open(sys.argv[1]).convert("RGBA")
for d, px in {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}.items():
    sq = full.resize((px, px), Image.LANCZOS)
    sq.save(f"{sys.argv[2]}/mipmap-{d}/ic_launcher.png")
    big = full.resize((px * 4, px * 4), Image.LANCZOS)
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, px * 4 - 1, px * 4 - 1), fill=255)
    big.putalpha(mask)
    big.resize((px, px), Image.LANCZOS).save(f"{sys.argv[2]}/mipmap-{d}/ic_launcher_round.png")
PY
rm -f "$here/.launcher-full.png"
