#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sh tests/run.sh
sh tests/run_storage.sh
bash tests/run-ui-format.sh
