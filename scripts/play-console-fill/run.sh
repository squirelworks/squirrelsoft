#!/usr/bin/env bash
# Thin wrapper: install deps if needed, then run fill.mjs with passed args.
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -d node_modules/playwright-core ]]; then
  npm install
fi
exec node fill.mjs "$@"
