#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/pebble-inspect-core.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
gcc -std=c99 -Wall -Wextra -Werror -pedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Isrc/c src/c/model.c src/c/codec.c tests/test_core.c -o "$test_binary"
"$test_binary"
