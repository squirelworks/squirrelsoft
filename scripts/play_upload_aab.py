#!/usr/bin/env python3
"""Upload Android App Bundles to Google Play (internal testing track by default).

Credentials: set GOOGLE_PLAY_SERVICE_ACCOUNT_JSON to a service-account key path.
Never embed the key in this repo.
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
DEFAULT_TRACK = "internal"


def die(msg: str, code: int = 1) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


def load_credentials(key_path: Path):
    if not key_path.is_file():
        die(f"service account JSON not found: {key_path}")
    with key_path.open() as f:
        data = json.load(f)
    email = data.get("client_email")
    project = data.get("project_id")
    if data.get("type") != "service_account" or not email or not data.get("private_key"):
        die("JSON is not a valid Google service-account key")
    print(f"auth: client_email={email} project_id={project}")
    return service_account.Credentials.from_service_account_file(
        str(key_path), scopes=SCOPES
    )


def humanize_http_error(exc: HttpError, package: str) -> str:
    status = getattr(exc.resp, "status", None)
    try:
        body = json.loads(exc.content.decode("utf-8"))
    except Exception:
        body = {"raw": exc.content.decode("utf-8", errors="replace")}
    message = (
        body.get("error", {}).get("message")
        if isinstance(body, dict)
        else None
    ) or str(exc)

    hints: list[str] = []
    lower = message.lower()
    if status in (401, 403) or "permission" in lower or "forbidden" in lower:
        hints.append(
            "Link the service account in Play Console → Users and permissions → "
            "Invite user → paste the service-account email with "
            "'Release apps' / 'Release to production' (or Admin)."
        )
    if status == 404 or "package not found" in lower or "app not found" in lower:
        hints.append(
            f"Create the app once in Play Console UI with package name `{package}` "
            "(All apps → Create app), then retry this upload."
        )
    if "apk" in lower and ("certificate" in lower or "signing" in lower):
        hints.append(
            "Signing key mismatch. Expected upload cert SHA-256 from prior notes: "
            "AC:62:05:DE:C3:E8:AD:41:8B:AA:5A:BE:9A:AE:90:4C:76:C8:89:20:B0:A1:2F:83:45:EE:3F:89:FC:00:F1:AB"
        )
    hint_txt = ("\n  next: " + " ".join(hints)) if hints else ""
    return f"HTTP {status}: {message}{hint_txt}"


def upload_one(
    service,
    *,
    package: str,
    aab_path: Path,
    track: str,
    version_code_hint: int | None,
) -> dict:
    result: dict = {
        "package": package,
        "aab": str(aab_path),
        "track": track,
        "versionCode_hint": version_code_hint,
        "ok": False,
    }
    if not aab_path.is_file():
        result["error"] = f"AAB missing: {aab_path}"
        return result

    print(f"\n=== {package} ← {aab_path.name} ===")
    try:
        edit = (
            service.edits()
            .insert(body={}, packageName=package)
            .execute()
        )
        edit_id = edit["id"]
        print(f"edit: {edit_id}")

        media = MediaFileUpload(
            str(aab_path),
            mimetype=mimetypes.guess_type(str(aab_path))[0]
            or "application/octet-stream",
            resumable=True,
        )
        bundle = (
            service.edits()
            .bundles()
            .upload(editId=edit_id, packageName=package, media_body=media)
            .execute()
        )
        version_code = bundle.get("versionCode")
        print(f"uploaded bundle versionCode={version_code}")

        track_body = {
            "track": track,
            "releases": [
                {
                    "name": f"v{version_code}",
                    "status": "completed",
                    "versionCodes": [str(version_code)],
                }
            ],
        }
        service.edits().tracks().update(
            editId=edit_id,
            packageName=package,
            track=track,
            body=track_body,
        ).execute()
        print(f"assigned to track={track}")

        commit = (
            service.edits()
            .commit(editId=edit_id, packageName=package)
            .execute()
        )
        print(f"committed edit id={commit.get('id', edit_id)}")
        result.update(
            {
                "ok": True,
                "editId": edit_id,
                "versionCode": version_code,
                "track": track,
            }
        )
        return result
    except HttpError as exc:
        result["error"] = humanize_http_error(exc, package)
        print(f"FAILED: {result['error']}", file=sys.stderr)
        return result
    except Exception as exc:  # noqa: BLE001 — report any API/client failure
        result["error"] = str(exc)
        print(f"FAILED: {exc}", file=sys.stderr)
        return result


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--package",
        action="append",
        dest="packages",
        metavar="NAME=PATH[:VERSIONCODE]",
        required=True,
        help="e.g. works.squirrel.suica=/path/app.aab:4",
    )
    p.add_argument(
        "--track",
        default=os.environ.get("GOOGLE_PLAY_TRACK", DEFAULT_TRACK),
        help=f"Play track (default: {DEFAULT_TRACK})",
    )
    p.add_argument(
        "--json-out",
        type=Path,
        help="Write machine-readable results JSON here",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    key = os.environ.get("GOOGLE_PLAY_SERVICE_ACCOUNT_JSON")
    if not key:
        die("Set GOOGLE_PLAY_SERVICE_ACCOUNT_JSON to the service-account JSON path")

    creds = load_credentials(Path(key))
    service = build("androidpublisher", "v3", credentials=creds, cache_discovery=False)

    results = []
    for spec in args.packages:
        if "=" not in spec:
            die(f"bad --package spec (want NAME=PATH[:VC]): {spec}")
        package, rest = spec.split("=", 1)
        version_hint = None
        if ":" in rest:
            # Allow Windows drive letters by splitting only on the last ':'
            # when the suffix is an integer versionCode.
            path_part, maybe_vc = rest.rsplit(":", 1)
            if maybe_vc.isdigit():
                aab_path = Path(path_part)
                version_hint = int(maybe_vc)
            else:
                aab_path = Path(rest)
        else:
            aab_path = Path(rest)
        results.append(
            upload_one(
                service,
                package=package,
                aab_path=aab_path,
                track=args.track,
                version_code_hint=version_hint,
            )
        )

    if args.json_out:
        args.json_out.write_text(json.dumps(results, indent=2) + "\n")
        print(f"\nwrote {args.json_out}")

    ok = all(r.get("ok") for r in results)
    print("\n=== summary ===")
    for r in results:
        if r.get("ok"):
            print(
                f"OK  {r['package']} track={r['track']} versionCode={r['versionCode']}"
            )
        else:
            print(f"ERR {r['package']}: {r.get('error')}")
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
