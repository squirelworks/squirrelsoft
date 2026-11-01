#!/usr/bin/env python3
"""Fill as much of the Play Console publishing checklist as the API allows.

Credentials: GOOGLE_PLAY_SERVICE_ACCOUNT_JSON (path to service-account key).
Never embed the key in this repo.

Via Android Publisher API this script can:
  - edits.details: contactEmail / contactWebsite / defaultLanguage
  - edits.listings: title / short / full description (delegates to LISTINGS)
  - edits.images: icon, featureGraphic, phoneScreenshots
  - applications.dataSafety: import a "no data collected" CSV

Cannot set via this API (Play Console UI required):
  - Privacy policy URL (Policy → App content → Privacy policy)
  - App category / tags
  - Content rating (IARC) questionnaire
  - Target audience / Designed for families
  - Ads declaration / Advertising ID declaration
  - Email-list testers (API only supports Google Groups)
  - Countries for the internal track (N/A; production only)
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

# Reuse listing copy from play_listing.py when available.
_SCRIPTS = Path(__file__).resolve().parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))
try:
    from play_listing import LISTINGS  # type: ignore
except Exception:  # noqa: BLE001
    LISTINGS = {}

SCOPES = ["https://www.googleapis.com/auth/androidpublisher"]

PACKAGES = {
    "works.squirrel.suica": {
        "stem": "suica",
        "category_hint": "TOOLS",
        "privacy_path": "docs/privacy-suica.html",
    },
    "works.squirrel.sound": {
        "stem": "sound",
        "category_hint": "MUSIC_AND_AUDIO",
        "privacy_path": "docs/privacy-sound.html",
    },
    "works.squirrel.qr": {
        "stem": "qr",
        "category_hint": "TOOLS",
        "privacy_path": "docs/privacy-qr.html",
    },
}

CONTACT_EMAIL = "squirelworks@gmail.com"
CONTACT_WEBSITE = "https://github.com/squirelworks/squirrelsoft"
DEFAULT_LANGUAGE = "en-US"

# jsDelivr serves raw HTML without GitHub login; update BRANCH after push.
PRIVACY_CDN_TMPL = (
    "https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@{branch}/{path}"
)

BLOCKED_CONSOLE_STEPS = [
    {
        "item": "Privacy policy URL",
        "path": "Play Console → App → Policy and programs → App content → Privacy policy → Manage",
        "action": "Paste the jsDelivr (or Pages) privacy HTML URL for that package, Save.",
    },
    {
        "item": "App category",
        "path": "Play Console → App → Grow users → Store presence → Store settings → App category",
        "action": "Suica/QR → Tools; Sound → Music & Audio. Add optional tags if offered.",
    },
    {
        "item": "Ads declaration",
        "path": "Play Console → App → Policy and programs → App content → Ads",
        "action": "Select No — this app does not contain ads.",
    },
    {
        "item": "Content rating (IARC)",
        "path": "Play Console → App → Policy and programs → App content → Content ratings → Start",
        "action": (
            "Email: squirelworks@gmail.com. Category: Utility / Productivity / Tools "
            "(or similar). All violence/sexual/UGC/gambling/drugs = No. "
            "NFC/mic/camera used only for core on-device features; no user-generated "
            "content sharing. Submit calculated ratings."
        ),
    },
    {
        "item": "Target audience",
        "path": "Play Console → App → Policy and programs → App content → Target audience and content",
        "action": "Age groups 18+ only (or 13+ if offered without children). Not Designed for Families. No appeal to children.",
    },
    {
        "item": "News / COVID / Data safety review submit",
        "path": "Play Console → App content remaining cards",
        "action": "News app = No. Government = No. Confirm Data safety shows 'No data collected' after API import; Submit if still draft.",
    },
    {
        "item": "Internal testers (email list)",
        "path": "Play Console → Test and release → Testing → Internal testing → Testers",
        "action": "Create/select an email list including squirelworks@gmail.com (API cannot set email lists; only Google Groups).",
    },
]


def die(msg: str, code: int = 1) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


def load_credentials(key_path: Path):
    if not key_path.is_file():
        die(f"service account JSON not found: {key_path}")
    return service_account.Credentials.from_service_account_file(
        str(key_path), scopes=SCOPES
    )


def privacy_url(branch: str, package: str) -> str:
    path = PACKAGES[package]["privacy_path"]
    return PRIVACY_CDN_TMPL.format(branch=branch, path=path)


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
            language=DEFAULT_LANGUAGE,
            imageType=image_type,
            media_body=media,
        )
        .execute()
    )


def replace_images(
    service, edit_id: str, package: str, image_type: str, paths: list[Path]
) -> int:
    try:
        service.edits().images().deleteall(
            editId=edit_id,
            packageName=package,
            language=DEFAULT_LANGUAGE,
            imageType=image_type,
        ).execute()
    except HttpError:
        pass
    count = 0
    for path in paths:
        if path.is_file():
            upload_image(service, edit_id, package, image_type, path)
            count += 1
            print(f"  uploaded {image_type}: {path.name}")
    return count


def update_listing(service, edit_id: str, package: str) -> bool:
    meta = LISTINGS.get(package)
    if not meta:
        print("  listing skipped (no LISTINGS entry)")
        return False
    service.edits().listings().update(
        editId=edit_id,
        packageName=package,
        language=meta.get("language", DEFAULT_LANGUAGE),
        body={
            "language": meta.get("language", DEFAULT_LANGUAGE),
            "title": meta["title"],
            "shortDescription": meta["shortDescription"],
            "fullDescription": meta["fullDescription"],
        },
    ).execute()
    print(f"  listing updated: {meta['title']}")
    return True


def update_details(service, edit_id: str, package: str, website: str) -> dict:
    body = {
        "defaultLanguage": DEFAULT_LANGUAGE,
        "contactEmail": CONTACT_EMAIL,
        "contactWebsite": website,
    }
    resp = (
        service.edits()
        .details()
        .update(editId=edit_id, packageName=package, body=body)
        .execute()
    )
    print(f"  details: email={resp.get('contactEmail')} website={resp.get('contactWebsite')}")
    return resp


def submit_data_safety(service, package: str, csv_text: str) -> dict:
    try:
        resp = (
            service.applications()
            .dataSafety(packageName=package, body={"safetyLabels": csv_text})
            .execute()
        )
        print("  dataSafety: OK")
        return {"ok": True, "response": resp or {}}
    except HttpError as exc:
        try:
            body = json.loads(exc.content.decode("utf-8"))
            msg = body.get("error", {}).get("message") or str(exc)
        except Exception:
            msg = str(exc)
        print(f"  dataSafety FAILED: HTTP {getattr(exc.resp, 'status', '?')}: {msg}", file=sys.stderr)
        return {"ok": False, "error": msg}


def inspect_track(service, package: str, track: str) -> dict:
    edit = service.edits().insert(body={}, packageName=package).execute()
    eid = edit["id"]
    try:
        track_resp = (
            service.edits()
            .tracks()
            .get(editId=eid, packageName=package, track=track)
            .execute()
        )
        details = service.edits().details().get(editId=eid, packageName=package).execute()
        shots = (
            service.edits()
            .images()
            .list(
                editId=eid,
                packageName=package,
                language=DEFAULT_LANGUAGE,
                imageType="phoneScreenshots",
            )
            .execute()
        )
        return {
            "track": track_resp,
            "details": details,
            "phoneScreenshots": len(shots.get("images") or []),
        }
    finally:
        try:
            service.edits().delete(editId=eid, packageName=package).execute()
        except HttpError:
            pass


def formup_package(
    service,
    *,
    package: str,
    graphics_dir: Path,
    csv_text: str | None,
    branch: str,
    skip_data_safety: bool,
) -> dict:
    cfg = PACKAGES[package]
    stem = cfg["stem"]
    result: dict = {
        "package": package,
        "ok": False,
        "privacyPolicyUrlSuggested": privacy_url(branch, package),
        "categoryHint": cfg["category_hint"],
    }
    icon = graphics_dir / f"{stem}-icon-512.png"
    feature = graphics_dir / f"{stem}-feature-1024x500.png"
    phones = sorted(graphics_dir.glob(f"{stem}-phone-*.png"))

    try:
        edit = service.edits().insert(body={}, packageName=package).execute()
        edit_id = edit["id"]
        print(f"\n=== {package} edit={edit_id} ===")

        website = privacy_url(branch, package)
        result["details"] = update_details(service, edit_id, package, website)
        result["listing"] = update_listing(service, edit_id, package)

        if icon.is_file():
            result["icon"] = replace_images(service, edit_id, package, "icon", [icon]) > 0
        if feature.is_file():
            result["featureGraphic"] = (
                replace_images(service, edit_id, package, "featureGraphic", [feature]) > 0
            )
        if phones:
            result["phoneScreenshots"] = replace_images(
                service, edit_id, package, "phoneScreenshots", phones
            )
        else:
            result["phoneScreenshots"] = 0
            print("  phone screenshots missing")

        commit = service.edits().commit(editId=edit_id, packageName=package).execute()
        result["editId"] = commit.get("id", edit_id)
        print(f"  committed edit id={result['editId']}")

        if csv_text and not skip_data_safety:
            result["dataSafety"] = submit_data_safety(service, package, csv_text)
        else:
            result["dataSafety"] = {"ok": None, "skipped": True}

        result["ok"] = True
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
        help="Package name (repeatable). Default: all three.",
    )
    ap.add_argument(
        "--graphics-dir",
        type=Path,
        default=Path("/opt/cursor/artifacts/play-graphics"),
    )
    ap.add_argument(
        "--data-safety-csv",
        type=Path,
        default=_SCRIPTS / "play_data" / "data_safety_no_data.csv",
    )
    ap.add_argument("--branch", default="main", help="Git branch for jsDelivr privacy URLs")
    ap.add_argument("--skip-data-safety", action="store_true")
    ap.add_argument("--inspect-only", action="store_true")
    ap.add_argument("--track", default="internal")
    ap.add_argument("--json-out", type=Path)
    args = ap.parse_args()

    key = os.environ.get("GOOGLE_PLAY_SERVICE_ACCOUNT_JSON")
    if not key:
        die("Set GOOGLE_PLAY_SERVICE_ACCOUNT_JSON")

    packages = args.packages or list(PACKAGES.keys())
    for p in packages:
        if p not in PACKAGES:
            die(f"unknown package: {p}")

    creds = load_credentials(Path(key))
    service = build("androidpublisher", "v3", credentials=creds, cache_discovery=False)

    csv_text = None
    if args.data_safety_csv.is_file() and not args.skip_data_safety:
        csv_text = args.data_safety_csv.read_text(encoding="utf-8")

    results = []
    if args.inspect_only:
        for package in packages:
            info = inspect_track(service, package, args.track)
            info["package"] = package
            info["privacyPolicyUrlSuggested"] = privacy_url(args.branch, package)
            results.append(info)
            print(json.dumps(info, indent=2)[:2000])
    else:
        for package in packages:
            results.append(
                formup_package(
                    service,
                    package=package,
                    graphics_dir=args.graphics_dir,
                    csv_text=csv_text,
                    branch=args.branch,
                    skip_data_safety=args.skip_data_safety,
                )
            )

    summary = {
        "results": results,
        "contactEmail": CONTACT_EMAIL,
        "privacyCdnTemplate": PRIVACY_CDN_TMPL,
        "branch": args.branch,
        "blockedConsoleSteps": BLOCKED_CONSOLE_STEPS,
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2) + "\n")
        print(f"\nwrote {args.json_out}")

    print("\n=== blocked Console steps (API cannot finish) ===")
    for step in BLOCKED_CONSOLE_STEPS:
        print(f"- {step['item']}: {step['path']}")

    if args.inspect_only:
        return 0
    return 0 if all(r.get("ok") for r in results) else 2


if __name__ == "__main__":
    raise SystemExit(main())
