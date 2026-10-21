#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/pebble-inspect-storage.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
gcc -std=c99 -Wall -Wextra -Werror -pedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/storage -Isrc/c src/c/model.c src/c/codec.c src/c/storage.c \
  tests/storage/test_storage.c -o "$test_binary"
"$test_binary"
