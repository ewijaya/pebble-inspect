.PHONY: all build test clean emulator screenshots
PEBBLE_PYTHON ?= $(HOME)/.local/share/uv/tools/pebble-tool/bin/python
APP_VERSION := $(shell python3 -c 'import json; print(json.load(open("package.json"))["version"])')
all: test build
build:
	sh scripts/build.sh
test:
	sh scripts/test.sh
clean:
	pebble clean --sdk 4.33.1
emulator:
	$(PEBBLE_PYTHON) scripts/emulator.py install artifacts/pebble-inspect-$(APP_VERSION).pbw
screenshots:
	$(PEBBLE_PYTHON) scripts/emulator.py screenshot --no-open docs/screenshots/overview.png
