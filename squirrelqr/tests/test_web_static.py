#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "web"
REQUIRED = [
    "index.html",
    "qr.js",
    "style.css",
    "manifest.webmanifest",
    "sw.js",
    "icon.svg",
]
FORBIDDEN = [
    "spectrum.html",
    "spectrum.js",
    "suica.html",
    "suica.js",
    "qr.html",
]


def main() -> int:
    fails = 0
    for name in REQUIRED:
        p = ROOT / name
        if not p.is_file() or p.stat().st_size < 20:
            print("FAIL missing/short", name)
            fails += 1
    for name in FORBIDDEN:
        if (ROOT / name).exists():
            print("FAIL specialized QR reader must not ship", name)
            fails += 1
    qr = (ROOT / "qr.js").read_text(encoding="utf-8")
    if "BarcodeDetector" not in qr:
        print("FAIL qr.js missing BarcodeDetector")
        fails += 1
    for needed in ("btnCopy", "btnClear", "btnStart", "btnStop"):
        if needed not in qr:
            print("FAIL qr.js missing", needed)
            fails += 1
    index = (ROOT / "index.html").read_text(encoding="utf-8")
    if "qr.js" not in index:
        print("FAIL index.html must host the QR reader")
        fails += 1
    for needed in ("btnStart", "btnStop", "btnCopy", "btnClear", "video", "result"):
        if f'id="{needed}"' not in index and f"id='{needed}'" not in index:
            # video/result use id without btn prefix
            if f'id="{needed}"' not in index:
                print("FAIL index missing scanner control", needed)
                fails += 1
    lowered = index.lower()
    css = (ROOT / "style.css").read_text(encoding="utf-8").lower()
    if "href=\"spectrum" in lowered or "live spectrum" in lowered:
        print("FAIL index still advertises spectrum as a QR reader feature")
        fails += 1
    if "suica" in lowered or "spectrum" in lowered or "felica" in lowered:
        print("FAIL index must not mention other products / field-kit features")
        fails += 1
    if "<nav" in lowered or 'class="nav"' in lowered:
        print("FAIL index still has multi-page nav chrome")
        fails += 1
    if "squirrelfield" in lowered or "sqfield" in lowered or "field kit" in lowered:
        print("FAIL index still uses Field / field-kit branding")
        fails += 1
    if ".tile" in css or ".grid" in css:
        print("FAIL style.css still has field-kit hub tile/grid styles")
        fails += 1
    manifest = (ROOT / "manifest.webmanifest").read_text(encoding="utf-8").lower()
    if "squirrelfield" in manifest or "sqfield" in manifest:
        print("FAIL PWA manifest still uses Field branding")
        fails += 1
    if "squirrelqr" not in manifest and "qr reader" not in manifest:
        print("FAIL PWA manifest should brand as SquirrelQR / QR reader")
        fails += 1
    if fails:
        return 1
    print("squirrelqr static tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
