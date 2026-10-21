#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
case "$(pebble --version)" in
  'Pebble Tool v5.0.40 '* ) ;;
  *) echo 'Expected Pebble Tool 5.0.40; see docs/setup.md' >&2; exit 1 ;;
esac
python3 scripts/icon.py
pebble build --sdk 4.33.1
python3 scripts/artifact.py
