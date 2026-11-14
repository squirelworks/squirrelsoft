#!/usr/bin/env python3
"""Docs honesty linter (CONTINUANCE §3c).

Every `- [x]` box in ROADMAP.md / TODO.md that names a check must find that
check in the make-test harness, and every check named by an open `- [ ]` box
must not exist yet. Cheap, deterministic, no network or QEMU.

A box "names a check" when its text contains:
  - an explicit "check(s) `name`" / "Check `name`" reference, or
  - a backtick token that is harness-shaped: a jni-smoke / tests path, a
    `*_smoke.sh` / `test_*.{c,py,sh}` script, a `make test*` / `test-*`
    target, or a CI gate env var (QR_*_FRAME, REQUIRE_*, PCSC_TEST_FLAGS).

Harness inventory comes from Makefiles, tests/ trees, jni-smoke dirs, and
CI workflow env references. Known historical mismatches can be listed in
tools/docs_lint_allowlist.txt without reticking many boxes.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC_NAMES = ("ROADMAP.md", "TODO.md")
ALLOWLIST_PATH = ROOT / "tools" / "docs_lint_allowlist.txt"

BOX_RE = re.compile(r"^(\s*)- \[([ xX])\]\s+(.*)$")
BACKTICK_RE = re.compile(r"`([^`]+)`")
# OS/2-style "Check `foo`" / "Checks `a`, `b`" (not "checked against `fn`").
EXPLICIT_CHECK_RE = re.compile(
    r"\bChecks?\s+`([^`]+)`"
)

# Harness-shaped backtick tokens (paths / targets / gate env vars).
HARNESS_PATH_RE = re.compile(
    r"(?:^|/)(?:android/)?jni-smoke(?:/|$)"
    r"|(?:^|/).*\btests?/"
    r"|(?:^|/)?[\w./-]*_smoke\.sh$"
    r"|(?:^|/)?(?:tests?/)?test_[\w.-]+\.(?:c|py|sh)$"
)
HARNESS_TARGET_RE = re.compile(
    r"^(?:make\s+)?test(?:-[\w-]+)?$|^android-test$"
)
HARNESS_ENV_RE = re.compile(
    r"^(?:QR_(?:WEBCAM|CAPTURE)_FRAME|REQUIRE_(?:LOOPBACK|NATIVE_LOOPBACK)|PCSC_TEST_FLAGS)$"
)
# CLI flags that the roadmap calls out as the CI probe for a feature.
CI_FLAG_RE = re.compile(r"^--(?:gui-snapshot|snapshot)$")

MAKE_TARGET_RE = re.compile(
    r"^([A-Za-z_][\w-]*)\s*:[^=]", re.MULTILINE
)
ENV_REF_RE = re.compile(
    r"\b(QR_WEBCAM_FRAME|QR_CAPTURE_FRAME|REQUIRE_LOOPBACK|"
    r"REQUIRE_NATIVE_LOOPBACK|PCSC_TEST_FLAGS)\b"
)


def repo_rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def load_allowlist(path: Path) -> set[tuple[str, str, str]]:
    """Return set of (rule, doc, check) triples. Empty file / missing = none.

    Allowlist line format (whitespace-separated):
      <rule> <doc> <check>
    where rule is missing-check or unexpected-check.
    Lines starting with # and blank lines are ignored.
    """
    out: set[tuple[str, str, str]] = set()
    if not path.is_file():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 2)
        if len(parts) != 3:
            raise SystemExit(
                f"docs_lint: bad allowlist line in {path}: {raw!r}"
            )
        rule, doc, check = parts
        if rule not in ("missing-check", "unexpected-check"):
            raise SystemExit(
                f"docs_lint: unknown allowlist rule {rule!r} in {path}"
            )
        out.add((rule, doc, check))
    return out


def is_harness_token(token: str) -> bool:
    t = token.strip()
    if not t:
        return False
    # Strip trailing punctuation leftovers that sometimes sneak into ticks.
    t = t.rstrip(".,;)")
    if HARNESS_ENV_RE.match(t) or HARNESS_TARGET_RE.match(t):
        return True
    if CI_FLAG_RE.match(t):
        return True
    # Paths: normalize leading ./ 
    path = t[2:] if t.startswith("./") else t
    if HARNESS_PATH_RE.search(path):
        return True
    return False


def extract_named_checks(body: str) -> list[str]:
    """Return unique check names named by a checkbox body, in order."""
    found: list[str] = []
    seen: set[str] = set()

    def add(name: str) -> None:
        name = name.strip().rstrip(".,;)")
        if not name or name in seen:
            return
        seen.add(name)
        found.append(name)

    for m in EXPLICIT_CHECK_RE.finditer(body):
        add(m.group(1) or m.group(2))

    for tok in BACKTICK_RE.findall(body):
        if is_harness_token(tok):
            add(tok)

    # "make test" without backticks still counts when written as the gate.
    if re.search(r"(?<![`\w])make test(?:-[\w]+)?(?![`\w-])", body):
        for m in re.finditer(r"\bmake test(?:-[\w]+)?\b", body):
            add(m.group(0))

    return found


def parse_boxes(doc: Path) -> list[dict]:
    boxes: list[dict] = []
    text = doc.read_text(encoding="utf-8")
    for lineno, line in enumerate(text.splitlines(), 1):
        m = BOX_RE.match(line)
        if not m:
            continue
        checked = m.group(2).lower() == "x"
        body = m.group(3)
        checks = extract_named_checks(body)
        if not checks:
            continue
        boxes.append(
            {
                "doc": repo_rel(doc),
                "lineno": lineno,
                "checked": checked,
                "body": body,
                "checks": checks,
            }
        )
    return boxes


def note_ci_flags(text: str, flags: set[str]) -> None:
    if "--gui-snapshot" in text:
        flags.add("--gui-snapshot")
    if re.search(r"(?<![\w-])--snapshot(?![\w-])", text):
        flags.add("--snapshot")


def collect_harness(root: Path) -> dict[str, set[str]]:
    """Build the set of known check names and supporting path/env/target sets."""
    paths: set[str] = set()
    targets: set[str] = set()
    envs: set[str] = set()
    flags: set[str] = set()

    # Makefiles and their test recipes.
    for mk in root.rglob("Makefile"):
        if "build" in mk.parts or ".git" in mk.parts:
            continue
        rel = repo_rel(mk)
        paths.add(rel)
        text = mk.read_text(encoding="utf-8", errors="replace")
        for m in MAKE_TARGET_RE.finditer(text):
            targets.add(m.group(1))
            targets.add(f"make {m.group(1)}")
        for m in ENV_REF_RE.finditer(text):
            envs.add(m.group(1))
        note_ci_flags(text, flags)

    # Test trees and jni-smoke.
    for pat in (
        "**/tests/**",
        "**/android/jni-smoke/**",
        "**/*_smoke.sh",
    ):
        for p in root.glob(pat):
            if ".git" in p.parts or "build" in p.parts:
                continue
            rel = repo_rel(p)
            paths.add(rel)
            # Also index parent dirs that boxes name (…/jni-smoke, …/tests).
            for parent in p.parents:
                if parent == root:
                    break
                paths.add(repo_rel(parent))
            if p.is_file() and p.suffix in (".sh", ".py", ".c", ".yml"):
                try:
                    text = p.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                for m in ENV_REF_RE.finditer(text):
                    envs.add(m.group(1))
                note_ci_flags(text, flags)

    # CI workflows (gate env vars and make test).
    for wf in (root / ".github" / "workflows").glob("*.yml"):
        text = wf.read_text(encoding="utf-8", errors="replace")
        paths.add(repo_rel(wf))
        for m in ENV_REF_RE.finditer(text):
            envs.add(m.group(1))
        if re.search(r"\bmake test\b", text):
            targets.add("test")
            targets.add("make test")

    # Root make test is the suite gate.
    if (root / "Makefile").is_file():
        targets.add("test")
        targets.add("make test")

    return {
        "paths": paths,
        "targets": targets,
        "envs": envs,
        "flags": flags,
    }


def check_exists(name: str, harness: dict[str, set[str]], root: Path) -> bool:
    n = name.strip().rstrip(".,;)")
    if n in harness["envs"] or n in harness["flags"]:
        return True
    if n in harness["targets"]:
        return True
    # Path existence (file or directory), exact or as prefix of a harness path.
    path = n[2:] if n.startswith("./") else n
    candidate = root / path
    if candidate.exists():
        return True
    if path in harness["paths"]:
        return True
    if any(p == path or p.startswith(path.rstrip("/") + "/") for p in harness["paths"]):
        return True
    # Bare script basename: test_capture.sh, webcam_smoke.sh
    base = Path(path).name
    if any(Path(p).name == base for p in harness["paths"]):
        return True
    return False


def lint(
    root: Path,
    allowlist: set[tuple[str, str, str]],
) -> tuple[list[str], list[str]]:
    """Return (errors, notes). errors non-empty => failure."""
    harness = collect_harness(root)
    errors: list[str] = []
    notes: list[str] = []
    boxes: list[dict] = []
    for name in DOC_NAMES:
        doc = root / name
        if doc.is_file():
            boxes.extend(parse_boxes(doc))

    used_allow: set[tuple[str, str, str]] = set()

    for box in boxes:
        for check in box["checks"]:
            exists = check_exists(check, harness, root)
            if box["checked"]:
                if exists:
                    continue
                key = ("missing-check", box["doc"], check)
                if key in allowlist:
                    used_allow.add(key)
                    notes.append(
                        f"allowlist missing-check {box['doc']}:{box['lineno']}"
                        f" `{check}`"
                    )
                    continue
                errors.append(
                    f"{box['doc']}:{box['lineno']}: checked box names `{check}` "
                    f"but that check is not in the make-test harness"
                )
            else:
                if not exists:
                    continue
                key = ("unexpected-check", box["doc"], check)
                if key in allowlist:
                    used_allow.add(key)
                    notes.append(
                        f"allowlist unexpected-check {box['doc']}:{box['lineno']}"
                        f" `{check}`"
                    )
                    continue
                errors.append(
                    f"{box['doc']}:{box['lineno']}: open box names `{check}` "
                    f"but that check already exists in the make-test harness"
                )

    unused = allowlist - used_allow
    for rule, doc, check in sorted(unused):
        notes.append(f"unused allowlist entry: {rule} {doc} {check}")

    return errors, notes


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--root",
        type=Path,
        default=ROOT,
        help="repository root (default: parent of tools/)",
    )
    ap.add_argument(
        "--allowlist",
        type=Path,
        default=None,
        help="allowlist file (default: tools/docs_lint_allowlist.txt)",
    )
    ap.add_argument(
        "--list-checks",
        action="store_true",
        help="print named checks from docs and exit 0",
    )
    args = ap.parse_args(argv)
    root = args.root.resolve()
    allow_path = args.allowlist or (root / "tools" / "docs_lint_allowlist.txt")
    allowlist = load_allowlist(allow_path)

    if args.list_checks:
        for name in DOC_NAMES:
            doc = root / name
            if not doc.is_file():
                continue
            for box in parse_boxes(doc):
                state = "x" if box["checked"] else " "
                checks = ", ".join(f"`{c}`" for c in box["checks"])
                print(f"{box['doc']}:{box['lineno']}: [{state}] {checks}")
        return 0

    errors, notes = lint(root, allowlist)
    for n in notes:
        print(f"note: {n}", file=sys.stderr)
    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        print(
            f"docs_lint: {len(errors)} honesty issue(s)",
            file=sys.stderr,
        )
        return 1

    nboxes = sum(
        1
        for name in DOC_NAMES
        if (root / name).is_file()
        for _ in parse_boxes(root / name)
    )
    print(f"docs_lint: ok ({nboxes} box(es) naming checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
