#!/usr/bin/env python3
"""Fill as much of the Play Console publishing checklist as the API allows.

Credentials: GOOGLE_PLAY_SERVICE_ACCOUNT_JSON (path to service-account key).
Never embed the key in this repo.

Via Android Publisher API this script can:
  - edits.details: contactEmail / contactWebsite / defaultLanguage
  - edits.listings: title / short / full description (delegates to LISTINGS)
  - edits.images: icon, featureGraphic, phoneScreenshots
  - applications.dataSafety: import a "no data collected" CSV

Cannot set via this API (Play Console UI required — see
docs/play-app-content-checklist.md for exact answers):
  - Privacy policy URL, App access / sign-in, Ads, Advertising ID
  - Content rating (IARC), Target audience, Government, Financial, Health
  - App category / tags; Data safety Console Submit if still draft
  - Email-list testers (API only supports Google Groups)
  - Countries for the internal track (N/A; production only)

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

# Reuse listing copy from play_listing.py when available.
_SCRIPTS = Path(__file__).resolve().parent
_REPO_ROOT = _SCRIPTS.parent
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
DEFAULT_PRIVACY_BRANCH = "main"

# jsDelivr serves raw HTML without GitHub login. Prefer @main so URLs survive
# feature-branch deletion.
PRIVACY_CDN_TMPL = (
    "https://cdn.jsdelivr.net/gh/squirelworks/squirrelsoft@{branch}/{path}"
)

REPO_GRAPHICS_DIR = _REPO_ROOT / "docs" / "play-graphics"
ARTIFACTS_GRAPHICS_DIR = Path("/opt/cursor/artifacts/play-graphics")

# Exact Console answers: docs/play-app-content-checklist.md
BLOCKED_CONSOLE_STEPS = [
    {
        "item": "Privacy policy URL",
        "path": "Play Console → App → Policy and programs → App content → Privacy policy → Manage",
        "action": (
            "Paste the jsDelivr @main privacy HTML URL for that package (see "
            "docs/README.md / docs/play-app-content-checklist.md), Save. "
            "API cannot set this field for Play-hosted apps."
        ),
    },
    {
        "item": "Sign-in details / App access",
        "path": "Play Console → App → Policy and programs → App content → App access",
        "action": (
            "All functionality available without special access; no accounts / "
            "sign-in / restricted sections. Do not add test credentials."
        ),
    },
    {
        "item": "Ads declaration",
        "path": "Play Console → App → Policy and programs → App content → Ads",
        "action": "Select No — this app does not contain ads. Advertising ID: not used.",
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
        "action": (
            "Age groups 18+ only (or consecutive 13+ bands without under-13). "
            "Not Designed for Families. No appeal to children."
        ),
    },
    {
        "item": "Government apps",
        "path": "Play Console → App → Policy and programs → App content → Government apps",
        "action": "No — not developed by or on behalf of a government.",
    },
    {
        "item": "Financial features",
        "path": "Play Console → App → Policy and programs → App content → Financial features",
        "action": (
            "None / does not provide financial features. Suica is read-only on-device "
            "NFC card display (does not move money or process payments)."
        ),
    },
    {
        "item": "Health",
        "path": "Play Console → App → Policy and programs → App content → Health",
        "action": "The app does not have any health features.",
    },
    {
        "item": "Data safety Console submit",
        "path": "Play Console → App content → Data safety",
        "action": (
            "API already imports no-data CSV; open Data safety and Submit/confirm "
            "if still draft."
        ),
    },
    {
        "item": "App category",
        "path": "Play Console → App → Grow users → Store presence → Store settings → App category",
        "action": "Suica/QR → Tools; Sound → Music & Audio. Add optional tags if offered.",
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
            "  Required files: {suica,sound,qr}-icon-512.png, "
            "*-feature-1024x500.png; optional *-phone-*.png"
        )

    if not path.is_dir():
        die(
            f"Graphics directory from {source} does not exist: {path}\n"
            f"Populate {REPO_GRAPHICS_DIR} or set PLAY_GRAPHICS_DIR / --graphics-dir."
        )
    print(f"graphics dir ({source}): {path}")
    return path


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


def update_details(service, edit_id: str, package: str) -> dict:
    """Set contact email + repo website. Privacy policy is Console-only."""
    body = {
        "defaultLanguage": DEFAULT_LANGUAGE,
        "contactEmail": CONTACT_EMAIL,
        "contactWebsite": CONTACT_WEBSITE,
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
        "contactWebsite": CONTACT_WEBSITE,
        "categoryHint": cfg["category_hint"],
    }
    icon = graphics_dir / f"{stem}-icon-512.png"
    feature = graphics_dir / f"{stem}-feature-1024x500.png"
    phones = sorted(graphics_dir.glob(f"{stem}-phone-*.png"))

    try:
        edit = service.edits().insert(body={}, packageName=package).execute()
        edit_id = edit["id"]
        print(f"\n=== {package} edit={edit_id} ===")
        print(f"  privacy (Console paste): {result['privacyPolicyUrlSuggested']}")

        result["details"] = update_details(service, edit_id, package)
        result["listing"] = update_listing(service, edit_id, package)

        if icon.is_file():
            result["icon"] = replace_images(service, edit_id, package, "icon", [icon]) > 0
        else:
            result["icon"] = False
            print(f"  WARNING: missing icon {icon.name} — skipping icon upload", file=sys.stderr)
        if feature.is_file():
            result["featureGraphic"] = (
                replace_images(service, edit_id, package, "featureGraphic", [feature]) > 0
            )
        else:
            result["featureGraphic"] = False
            print(
                f"  WARNING: missing feature graphic {feature.name} — skipping",
                file=sys.stderr,
            )
        if phones:
            result["phoneScreenshots"] = replace_images(
                service, edit_id, package, "phoneScreenshots", phones
            )
        else:
            result["phoneScreenshots"] = 0
            print(
                f"  WARNING: no {stem}-phone-*.png under {graphics_dir} — "
                "skipping phone screenshots",
                file=sys.stderr,
            )

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
        default=None,
        help=(
            "Directory with {suica,sound,qr}-icon-512.png and *-feature-1024x500.png. "
            "Default: docs/play-graphics/, else PLAY_GRAPHICS_DIR, else "
            "/opt/cursor/artifacts/play-graphics/."
        ),
    )
    ap.add_argument(
        "--data-safety-csv",
        type=Path,
        default=_SCRIPTS / "play_data" / "data_safety_no_data.csv",
    )
    ap.add_argument(
        "--branch",
        default=DEFAULT_PRIVACY_BRANCH,
        help="Git ref for jsDelivr privacy URLs (default: main)",
    )
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

    graphics_dir = resolve_graphics_dir(args.graphics_dir)

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
            info["contactWebsiteExpected"] = CONTACT_WEBSITE
            results.append(info)
            print(json.dumps(info, indent=2)[:2000])
    else:
        for package in packages:
            results.append(
                formup_package(
                    service,
                    package=package,
                    graphics_dir=graphics_dir,
                    csv_text=csv_text,
                    branch=args.branch,
                    skip_data_safety=args.skip_data_safety,
                )
            )

    summary = {
        "results": results,
        "contactEmail": CONTACT_EMAIL,
        "contactWebsite": CONTACT_WEBSITE,
        "privacyCdnTemplate": PRIVACY_CDN_TMPL,
        "branch": args.branch,
        "graphicsDir": str(graphics_dir),
        "blockedConsoleSteps": BLOCKED_CONSOLE_STEPS,
        "privacyUrlsForConsolePaste": {
            pkg: privacy_url(args.branch, pkg) for pkg in packages
        },
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2) + "\n")
        print(f"\nwrote {args.json_out}")

    print("\n=== privacy URLs (paste in Console — API cannot set) ===")
    for pkg in packages:
        print(f"- {pkg}: {privacy_url(args.branch, pkg)}")

    print("\n=== blocked Console steps (API cannot finish) ===")
    for step in BLOCKED_CONSOLE_STEPS:
        print(f"- {step['item']}: {step['path']}")

    if args.inspect_only:
        return 0
    return 0 if all(r.get("ok") for r in results) else 2


if __name__ == "__main__":
    raise SystemExit(main())
