#!/usr/bin/env bash
# S6 prep: print path inventories and suggested git filter-repo argument lines
# for the future per-app repositories. Does NOT create, delete, or push any
# GitHub repository. Squirrel runs filter-repo and creates the remotes.
#
# Usage:
#   scripts/s6_repo_split_prep.sh              # human-readable inventory
#   scripts/s6_repo_split_prep.sh --filter-repo  # one filter-repo --path line per repo
#   scripts/s6_repo_split_prep.sh --names       # future repo names only
#   scripts/s6_repo_split_prep.sh --check       # verify listed paths exist on disk
#
# Companion write-up: docs/s6-repo-split.md
# Suggested CI stubs: docs/s6-ci-stubs/
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

mode=inventory
case "${1:-}" in
  --filter-repo) mode=filter-repo ;;
  --names) mode=names ;;
  --check) mode=check ;;
  -h|--help)
    sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  "") ;;
  *)
    echo "unknown option: $1 (try --help)" >&2
    exit 2
    ;;
esac

# Future repo name → space-separated paths relative to the umbrella root.
# Shared libs that several apps need are listed on each consumer; at split
# time Squirrel vendors or submodules them (umbrella keeps the canonical copy).
# See docs/s6-repo-split.md.

declare -A REPO_PATHS
REPO_PATHS[squirrelsoft-suica]="squirrelsuica libs/sqfelica"
REPO_PATHS[squirrelsoft-suica-analysis]="squirrelsuica-analysis libs/sqfelica"
REPO_PATHS[squirrelsoft-soundanalysis]="squirrelsound libs/sqdsp libs/sqgui"
REPO_PATHS[squirrelsoft-qr]="squirrelqr libs/sqqr libs/sqgui"
REPO_PATHS[squirrelsoft-cad]="squirrelstation squirrelcad libs/sqgui"
REPO_PATHS[squirrelsoft-gnss]="squirrelgnss"
REPO_PATHS[squirrelsoft-calc]="squirrelcalc libs/sqmath libs/sqgui"
REPO_PATHS[squirrelsoft-games]="squirrelgames libs/sqgui"

# Stable print order (ROADMAP S6 + SuicaAnalysis).
REPO_ORDER=(
  squirrelsoft-suica
  squirrelsoft-suica-analysis
  squirrelsoft-soundanalysis
  squirrelsoft-qr
  squirrelsoft-cad
  squirrelsoft-gnss
  squirrelsoft-calc
  squirrelsoft-games
)

# Paths that stay in the umbrella after the split (not exhaustive; docs/ and
# scripts/ Play helpers stay here by design).
UMBRELLA_KEEP=(
  libs
  scripts
  docs
  .github
  squirrelpaint
  squirreldebug
  workspaces
  Makefile
  README.md
  ROADMAP.md
  HANDOFF.md
  AGENTS.md
  LICENSE
)

print_paths() {
  local paths="$1"
  local p
  for p in $paths; do
    printf '  - %s\n' "$p"
  done
}

filter_repo_args() {
  local paths="$1"
  local p
  for p in $paths; do
    printf ' --path %s' "$p"
  done
}

missing=0
check_paths() {
  local name="$1"
  local paths="$2"
  local p
  for p in $paths; do
    if [[ ! -e "$p" ]]; then
      echo "MISSING ($name): $p" >&2
      missing=1
    else
      echo "ok ($name): $p"
    fi
  done
}

case "$mode" in
  names)
    printf '%s\n' "${REPO_ORDER[@]}"
    ;;
  filter-repo)
    echo "# Dry-run helpers only. Squirrel clones a fresh mirror, then runs"
    echo "# git filter-repo with these --path flags. Do not run against origin/main."
    echo
    for name in "${REPO_ORDER[@]}"; do
      paths="${REPO_PATHS[$name]}"
      echo "# $name"
      echo "git filter-repo$(filter_repo_args "$paths")"
      echo
    done
    ;;
  check)
    for name in "${REPO_ORDER[@]}"; do
      check_paths "$name" "${REPO_PATHS[$name]}"
    done
    for p in "${UMBRELLA_KEEP[@]}"; do
      if [[ ! -e "$p" ]]; then
        echo "MISSING (umbrella): $p" >&2
        missing=1
      else
        echo "ok (umbrella): $p"
      fi
    done
    exit "$missing"
    ;;
  inventory)
    echo "S6 repo-split path inventory (prep only; no remotes touched)"
    echo "Root: $root"
    echo "Write-up: docs/s6-repo-split.md"
    echo "CI stubs: docs/s6-ci-stubs/"
    echo
    for name in "${REPO_ORDER[@]}"; do
      paths="${REPO_PATHS[$name]}"
      echo "## $name"
      print_paths "$paths"
      echo "  filter-repo: git filter-repo$(filter_repo_args "$paths")"
      echo
    done
    echo "## umbrella (squirrelsoft) keeps"
    for p in "${UMBRELLA_KEEP[@]}"; do
      printf '  - %s\n' "$p"
    done
    echo
    echo "Notes:"
    echo "  - squirrelsoft-suica-analysis still imports ../squirrelsuica/felica_parse.py"
    echo "    at runtime; either co-ship a copy, submodule squirrelsoft-suica, or"
    echo "    finish the sqfelica-only path before the split."
    echo "  - Shared libs (sqgui, sqfelica, …) are duplicated in path lists on purpose;"
    echo "    umbrella remains canonical; consumers vendor or submodule at split."
    echo "  - applicationId values must never change (see docs/s6-repo-split.md)."
    ;;
esac
