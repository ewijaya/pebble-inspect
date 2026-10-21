#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp /tmp/inspect-ui-format.XXXXXX)"
trap 'rm -f "$test_binary"' EXIT
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Isrc/c src/c/ui_format.c tests/test_ui_format.c -o "$test_binary"
"$test_binary"
