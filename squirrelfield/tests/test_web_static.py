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
            print("FAIL specialized Field must not ship", name)
            fails += 1
    qr = (ROOT / "qr.js").read_text(encoding="utf-8")
    if "BarcodeDetector" not in qr:
        print("FAIL qr.js missing BarcodeDetector")
        fails += 1
    index = (ROOT / "index.html").read_text(encoding="utf-8")
    if "qr.js" not in index:
        print("FAIL index.html must host the QR reader")
        fails += 1
    lowered = index.lower()
    if "href=\"spectrum" in lowered or "live spectrum" in lowered:
        print("FAIL index still advertises spectrum as a Field feature")
        fails += 1
    if "suica.html" in lowered:
        print("FAIL index still links Suica UI")
        fails += 1
    if fails:
        return 1
    print("squirrelfield static tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
