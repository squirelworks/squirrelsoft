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
