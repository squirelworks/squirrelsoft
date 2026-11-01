#!/usr/bin/env python3
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "web"
REQUIRED = [
    "index.html",
    "spectrum.html",
    "qr.html",
    "suica.html",
    "spectrum.js",
    "qr.js",
    "suica.js",
    "style.css",
    "manifest.webmanifest",
    "sw.js",
    "icon.svg",
]


def main() -> int:
    fails = 0
    for name in REQUIRED:
        p = ROOT / name
        if not p.is_file() or p.stat().st_size < 20:
            print("FAIL missing/short", name)
            fails += 1
    # Basic content checks
    qr = (ROOT / "qr.js").read_text(encoding="utf-8")
    if "BarcodeDetector" not in qr:
        print("FAIL qr.js missing BarcodeDetector")
        fails += 1
    sp = (ROOT / "spectrum.js").read_text(encoding="utf-8")
    if "getUserMedia" not in sp or "waterfall" not in sp:
        print("FAIL spectrum.js missing mic/waterfall")
        fails += 1
    su = (ROOT / "suica.js").read_text(encoding="utf-8")
    if "balance_yen" not in su:
        print("FAIL suica.js missing demo balance")
        fails += 1
    if fails:
        return 1
    print("squirrelfield static tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
