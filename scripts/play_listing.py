#!/usr/bin/env python3
"""Update Google Play store listings + graphics via Android Publisher API.

Credentials: GOOGLE_PLAY_SERVICE_ACCOUNT_JSON (path to service-account key).
Never embed the key in this repo.

What this can set via API:
  - listings (title, shortDescription, fullDescription) for a language
  - images: icon (512), featureGraphic (1024x500)
  - track release notes (on an existing edit / with a new edit)

Related: scripts/play_formup.py also sets contact details, phone screenshots,
and applications.dataSafety. Privacy policy URL / category / IARC / ads /
target audience / email-list testers still need Play Console UI.

Graphics: default docs/play-graphics/ (repo), else PLAY_GRAPHICS_DIR /
--graphics-dir, else /opt/cursor/artifacts/play-graphics/. Missing dir fails.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import sys
from pathlib import Path

from google.oauth2 import service_account
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from googleapiclient.http import MediaFileUpload

SCOPES = ["https://www.googleapis.com/auth/androidpublisher"]

_SCRIPTS = Path(__file__).resolve().parent
_REPO_ROOT = _SCRIPTS.parent
REPO_GRAPHICS_DIR = _REPO_ROOT / "docs" / "play-graphics"
ARTIFACTS_GRAPHICS_DIR = Path("/opt/cursor/artifacts/play-graphics")
CONTACT_WEBSITE = "https://github.com/squirelworks/squirrelsoft"
PRIVACY_URLS = {
    "works.squirrel.suica": "https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-suica.html",
    "works.squirrel.sound": "https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-sound.html",
    "works.squirrel.qr": "https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-qr.html",
    "works.squirrel.suicaanalysis": "https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@main/docs/privacy-suica-analysis.html",
}

LISTINGS = {
    "works.squirrel.suica": {
        "language": "en-US",
        "title": "SquirrelSuica",
        "shortDescription": "Read Suica, PASMO, nanaco, Edy & WAON balances and history over NFC.",
        "fullDescription": (
            "SquirrelSuica is a read-only FeliCa helper for Japanese "
            "transit and e-money cards.\n\n"
            "• Suica / PASMO / ICOCA-class transit IC — balance and journey history "
            "with station names\n"
            "• nanaco, Rakuten Edy, and WAON — balance and purchase/charge history\n"
            "• Works only while the app is open (no auto-launch when a card brushes "
            "the phone)\n"
            "• No writes, no cloning, no network card upload\n\n"
            "Hold your card still on the NFC spot with the app in the foreground. "
            "History uses publicly documented FeliCa layouts; station names come "
            "from the community Saibane station-code table.\n\n"
            "Requires an NFC-F capable phone."
        ),
        "releaseNotes": (
            "Fix Suica/nanaco history: read service 0x090f (not 0x010f), balance at "
            "bytes 10–11. nanaco/Edy/WAON history parsers. New branded launcher icon. "
            "Removed demo-card buttons."
        ),
    },
    "works.squirrel.sound": {
        "language": "en-US",
        "title": "SquirrelSound",
        "shortDescription": "Live microphone spectrum analyzer — see your audio in real time.",
        "fullDescription": (
            "SquirrelSound turns your phone mic into a live spectrum display.\n\n"
            "• Real-time FFT spectrum view\n"
            "• Lightweight, on-device only — audio stays on your phone\n"
            "• Built for musicians, engineers, and curious listeners\n\n"
            "Grant microphone permission when prompted. No account required."
        ),
        "releaseNotes": "Branded spectrum launcher icon. Internal testing build.",
    },
    "works.squirrel.qr": {
        "language": "en-US",
        "title": "SquirrelQR",
        "shortDescription": "Fast on-device QR code scanner. Camera only — no account.",
        "fullDescription": (
            "SquirrelQR is a focused QR code reader.\n\n"
            "• Point the camera at a QR code to decode instantly\n"
            "• On-device processing — scans are not uploaded by the app\n"
            "• Simple, distraction-free UI\n\n"
            "Camera permission required. No account, no ads in this build."
        ),
        "releaseNotes": "Branded QR launcher icon. Internal testing build.",
    },
    "works.squirrel.suicaanalysis": {
        "language": "en-US",
        "title": "SquirrelSuicaAnalysis",
        "shortDescription": "Read-only FeliCa debug dump: every service, block and raw command. No keys.",
        "fullDescription": (
            "SquirrelSuicaAnalysis is a read-only FeliCa debug reader that shows "
            "exactly what a transit or e-money card answers.\n\n"
            "• Walks every system, area and service the card lists, and reads every "
            "block that needs no key\n"
            "• Tries read sizes from 1 to 15 blocks and records where the card stops, "
            "with status flags and latency\n"
            "• Decodes the PMm into IC type and maximum response times\n"
            "• Summary and raw Command log tabs; share or save the dump as JSON\n"
            "• Redact IDm is on by default and removes the card ID from exports, "
            "raw frames included\n"
            "• No writes, no keys, no network permission\n\n"
            "Hold the card still on the NFC spot with the app in the foreground. "
            "Built to collect real card data for SquirrelSuica.\n\n"
            "Requires an NFC-F capable phone."
        ),
        "releaseNotes": "First internal testing build.",
    },
}

MANUAL_CONSOLE_STEPS = [
    "Privacy policy URL (Policy and programs → App content → Privacy policy) — paste @main jsDelivr URLs from docs/README.md",
    "App category (Store settings) — contact email/website (repo URL) via edits.details / play_formup.py",
    "Content rating questionnaire (IARC)",
    "Target audience / Designed for families answers",
    "Ads / Advertising ID declaration",
    "Confirm Data safety submitted in Console (CSV can be written via API)",
    "Internal testing email-list testers (API only supports Google Groups)",
    "Countries / pricing / production promote (when leaving internal)",
]


def die(msg: str, code: int = 1) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


def resolve_graphics_dir(explicit: Path | None) -> Path:
    """Resolve store-graphics directory; fail loudly when nothing usable exists."""
    if explicit is not None:
        path = explicit.expanduser()
        source = "--graphics-dir"
    elif os.environ.get("PLAY_GRAPHICS_DIR"):
        path = Path(os.environ["PLAY_GRAPHICS_DIR"]).expanduser()
        source = "PLAY_GRAPHICS_DIR"
    elif REPO_GRAPHICS_DIR.is_dir():
        path = REPO_GRAPHICS_DIR
        source = "repo docs/play-graphics"
    elif ARTIFACTS_GRAPHICS_DIR.is_dir():
        path = ARTIFACTS_GRAPHICS_DIR
        source = "artifacts fallback"
        print(
            f"WARNING: graphics dir missing at {REPO_GRAPHICS_DIR}; "
            f"falling back to {path}. Prefer committing icons under "
            f"docs/play-graphics/ or set PLAY_GRAPHICS_DIR / --graphics-dir.",
            file=sys.stderr,
        )
    else:
        die(
            "Graphics directory not found.\n"
            f"  Expected repo path: {REPO_GRAPHICS_DIR}\n"
            f"  Or cloud artifacts:  {ARTIFACTS_GRAPHICS_DIR}\n"
            "  Or set --graphics-dir / PLAY_GRAPHICS_DIR.\n"
            "  Required files: {suica,sound,qr}-icon-512.png, *-feature-1024x500.png"
        )

    if not path.is_dir():
        die(
            f"Graphics directory from {source} does not exist: {path}\n"
            f"Populate {REPO_GRAPHICS_DIR} or set PLAY_GRAPHICS_DIR / --graphics-dir."
        )
    print(f"graphics dir ({source}): {path}")
    return path


def load_credentials(key_path: Path):
    if not key_path.is_file():
        die(f"service account JSON not found: {key_path}")
    return service_account.Credentials.from_service_account_file(
        str(key_path), scopes=SCOPES
    )


def upload_image(service, edit_id: str, package: str, image_type: str, path: Path) -> dict:
    media = MediaFileUpload(
        str(path),
        mimetype=mimetypes.guess_type(str(path))[0] or "image/png",
        resumable=True,
    )
    return (
        service.edits()
        .images()
        .upload(
            editId=edit_id,
            packageName=package,
            language=LISTINGS[package]["language"],
            imageType=image_type,
            media_body=media,
        )
        .execute()
    )


def update_package(
    service,
    *,
    package: str,
    icon: Path | None,
    feature: Path | None,
    version_code: int | None,
    track: str,
) -> dict:
    meta = LISTINGS[package]
    result: dict = {"package": package, "ok": False}
    try:
        edit = service.edits().insert(body={}, packageName=package).execute()
        edit_id = edit["id"]
        print(f"\n=== {package} edit={edit_id} ===")

        service.edits().listings().update(
            editId=edit_id,
            packageName=package,
            language=meta["language"],
            body={
                "language": meta["language"],
                "title": meta["title"],
                "shortDescription": meta["shortDescription"],
                "fullDescription": meta["fullDescription"],
            },
        ).execute()
        print(f"listing updated ({meta['language']})")
        result["listing"] = {
            "title": meta["title"],
            "shortDescription": meta["shortDescription"],
        }

        if icon and icon.is_file():
            # Clear previous icons then upload (Play keeps multiple otherwise).
            try:
                service.edits().images().deleteall(
                    editId=edit_id,
                    packageName=package,
                    language=meta["language"],
                    imageType="icon",
                ).execute()
            except HttpError:
                pass
            up = upload_image(service, edit_id, package, "icon", icon)
            print(f"icon uploaded: {up.get('image', {}).get('url', up)}")
            result["icon"] = True
        elif icon is not None:
            print(f"WARNING: missing icon {icon} — skipping", file=sys.stderr)
            result["icon"] = False
        if feature and feature.is_file():
            try:
                service.edits().images().deleteall(
                    editId=edit_id,
                    packageName=package,
                    language=meta["language"],
                    imageType="featureGraphic",
                ).execute()
            except HttpError:
                pass
            up = upload_image(service, edit_id, package, "featureGraphic", feature)
            print(f"featureGraphic uploaded: {up.get('image', {}).get('url', up)}")
            result["featureGraphic"] = True
        elif feature is not None:
            print(f"WARNING: missing feature graphic {feature} — skipping", file=sys.stderr)
            result["featureGraphic"] = False

        if version_code is not None:
            # Attach release notes to the internal track release that already has this VC.
            track_resp = (
                service.edits()
                .tracks()
                .get(editId=edit_id, packageName=package, track=track)
                .execute()
            )
            releases = track_resp.get("releases") or []
            updated = False
            for rel in releases:
                vcs = [str(v) for v in rel.get("versionCodes") or []]
                if str(version_code) in vcs:
                    rel["releaseNotes"] = [
                        {"language": meta["language"], "text": meta["releaseNotes"]}
                    ]
                    updated = True
            if not updated:
                # Create/update a completed release note entry referencing the VC.
                releases.append(
                    {
                        "name": f"v{version_code}",
                        "status": "completed",
                        "versionCodes": [str(version_code)],
                        "releaseNotes": [
                            {"language": meta["language"], "text": meta["releaseNotes"]}
                        ],
                    }
                )
            service.edits().tracks().update(
                editId=edit_id,
                packageName=package,
                track=track,
                body={"track": track, "releases": releases},
            ).execute()
            print(f"release notes set on track={track} vc={version_code}")
            result["releaseNotes"] = meta["releaseNotes"]

        commit = service.edits().commit(editId=edit_id, packageName=package).execute()
        print(f"committed edit id={commit.get('id', edit_id)}")
        result["ok"] = True
        result["editId"] = edit_id
        return result
    except HttpError as exc:
        try:
            body = json.loads(exc.content.decode("utf-8"))
            msg = body.get("error", {}).get("message") or str(exc)
        except Exception:
            msg = str(exc)
        result["error"] = f"HTTP {getattr(exc.resp, 'status', '?')}: {msg}"
        print(f"FAILED: {result['error']}", file=sys.stderr)
        return result
    except Exception as exc:  # noqa: BLE001
        result["error"] = str(exc)
        print(f"FAILED: {exc}", file=sys.stderr)
        return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--package",
        action="append",
        dest="packages",
        required=True,
        help="Package name (repeatable). Optional =vc suffix, e.g. works.squirrel.suica=5",
    )
    ap.add_argument(
        "--graphics-dir",
        type=Path,
        default=None,
        help=(
            "Directory with {suica,sound,qr,suicaanalysis}-icon-512.png and *-feature-1024x500.png. "
            "Default: docs/play-graphics/, else PLAY_GRAPHICS_DIR, else "
            "/opt/cursor/artifacts/play-graphics/."
        ),
    )
    ap.add_argument("--track", default="internal")
    ap.add_argument("--json-out", type=Path)
    args = ap.parse_args()

    key = os.environ.get("GOOGLE_PLAY_SERVICE_ACCOUNT_JSON")
    if not key:
        die("Set GOOGLE_PLAY_SERVICE_ACCOUNT_JSON")

    graphics_dir = resolve_graphics_dir(args.graphics_dir)

    creds = load_credentials(Path(key))
    service = build("androidpublisher", "v3", credentials=creds, cache_discovery=False)

    stem = {
        "works.squirrel.suica": "suica",
        "works.squirrel.sound": "sound",
        "works.squirrel.qr": "qr",
        "works.squirrel.suicaanalysis": "suicaanalysis",
    }

    results = []
    for spec in args.packages:
        version_code = None
        package = spec
        if "=" in spec:
            package, vc = spec.split("=", 1)
            if vc.isdigit():
                version_code = int(vc)
        if package not in LISTINGS:
            die(f"unknown package (add to LISTINGS): {package}")
        s = stem[package]
        icon = graphics_dir / f"{s}-icon-512.png"
        feature = graphics_dir / f"{s}-feature-1024x500.png"
        results.append(
            update_package(
                service,
                package=package,
                icon=icon,
                feature=feature,
                version_code=version_code,
                track=args.track,
            )
        )

    summary = {
        "results": results,
        "contactWebsite": CONTACT_WEBSITE,
        "privacyUrlsForConsolePaste": PRIVACY_URLS,
        "graphicsDir": str(graphics_dir),
        "manualConsoleStepsStillRequired": MANUAL_CONSOLE_STEPS,
    }
    if args.json_out:
        args.json_out.write_text(json.dumps(summary, indent=2) + "\n")
        print(f"\nwrote {args.json_out}")

    print("\n=== privacy URLs (Console paste; contactWebsite = repo URL) ===")
    for pkg, url in PRIVACY_URLS.items():
        print(f"- {pkg}: {url}")
    print(f"contactWebsite: {CONTACT_WEBSITE}")

    print("\n=== manual Console steps still required ===")
    for step in MANUAL_CONSOLE_STEPS:
        print(f"- {step}")

    return 0 if all(r.get("ok") for r in results) else 2


if __name__ == "__main__":
    raise SystemExit(main())
