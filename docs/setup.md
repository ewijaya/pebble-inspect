# Reproducible development setup

Verified on 2026-10-21: Ubuntu 22.04.5, Linux 6.8.0-1061-aws,
**x86_64 AWS instance, not ARM/Graviton**, glibc 2.35. At audit: 15 GiB
RAM (11 GiB available), 2 GiB swap, 9.9 GiB free on the project volume.
Python 3.10.12, Node 24.21.0, npm 11.19.0, Git 2.34.1, uv 0.9.26,
host GCC 11.4.0. Docker and Podman were not installed; virtualization
reports `amazon`. Native tools work, so containers are unnecessary here.

Pinned build tools:

| Tool | Version |
| --- | --- |
| Pebble Tool | 5.0.40 |
| Pebble watchapp SDK | 4.33.1 |
| Bundled ARM cross compiler | xPack GNU Arm Embedded GCC 14.2.1 (20241119), x86_64 host |
| Bundled emulator | QEMU 10.1.5-pebble14 |
| SDK metadata in package.json | `sdkVersion: "3"` (template format, **not** installed SDK version) |

This is the watchapp SDK, not the PebbleOS firmware build environment.
No firmware toolchain is needed for the default build. No system Python,
glibc, global SDK, or unrelated repository was replaced.

For another Ubuntu 22.04 x86_64 server, install the documented native libraries,
Python 3.10 and Node, then use an isolated CLI environment:

```sh
sudo apt-get update
sudo apt-get install -y python3 python3-venv build-essential make \
  libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 zlib1g libsndio7.0
# Install uv by your normal trusted method; Node must already be on PATH.
uv tool install --python /usr/bin/python3 pebble-tool==5.0.40
export PATH="$HOME/.local/bin:$PATH"
pebble sdk install 4.33.1
pebble --version
make
```

The installed CLI's SDK service lists 4.33.1 (confirmed with its actual HTTP
client). `pebble sdk list` omits already installed versions from its remote
listing. Do not replace it with an older version because of that display.
The build selects `--sdk 4.33.1` explicitly, independently of the global active
SDK. There are no npm app dependencies and no npm install step.
An ARM server needs an actually supported ARM toolchain bundle or an x86_64
runner; this project has only validated the x86_64 environment above.

`make` runs host tests and builds/copies the PBW. Build only: `make build`.
Core tests only: `make test`. The template Waf build file comes from
`pebble new-project --c --simple` in CLI 5.0.40. Original application and icon
code were written for Inspect; reference project code was not copied.
The SDK handles its own licensed headers, compiler, and resource pipeline.

## Isolated headless emulator

The official [AI guide](https://developer.repebble.com/guides/ai-agents/)
requires `--vnc` for headless emulator commands. CLI 5.0.40 exposes that flag,
but starts some listeners on wildcard addresses and reuses global emulator
state. This repository provides a narrowly scoped adapter, `scripts/emulator.py`,
which calls the same installed CLI with `--emulator emery --sdk 4.33.1 --vnc`.
It isolates both XDG data and the CLI's temporary process registry in ignored
`.emulator/`, reuses the installed SDK by symlink,
and restricts QEMU serial/GDB/monitor/VNC, websockify and pypkjs to loopback.
It neither copies credentials nor edits the installed tools.

```sh
make emulator
make screenshots
# More CLI operations, always using the same isolated state:
~/.local/share/uv/tools/pebble-tool/bin/python scripts/emulator.py emu-button click select
~/.local/share/uv/tools/pebble-tool/bin/python scripts/emulator.py logs --no-color
~/.local/share/uv/tools/pebble-tool/bin/python scripts/emulator.py kill
```

Override `PEBBLE_PYTHON` when the CLI environment is elsewhere. The adapter
refuses unexpected CLI versions and legacy `~/.pebble-sdk` layouts rather than
silently controlling an unrelated emulator. VNC uses localhost:5901 and its
websocket proxy localhost:6080; reserve those ports for this session. Never
open these ports in an AWS security group. Stop only the isolated session.

References: [SDK installation](https://developer.repebble.com/sdk/),
[documentation index](https://developer.repebble.com/llms.txt),
[C API](https://developer.repebble.com/docs/c/).

## Workspace and references

The requested directory existed but was empty and was not a Git repository.
No ancestor `AGENTS.md` or `CLAUDE.md` applied. Development initially used local
branch `feat/inspect-v0.1` with no remote. Edward subsequently authorized public
GitHub publication as `ewijaya/pebble-inspect`; the initial published branch is
`main`.
The following reference revisions were inspected for conventions:

| Repository | Revision | Conventions |
| --- | --- | --- |
| [pebble-dev](https://github.com/ewijaya/pebble-dev) | `4dd0ccdfd8cea4b12c9c37f9c82ec30f7759be3e` | Native resources and release evidence |
| [pebble-orationes](https://github.com/ewijaya/pebble-orationes) | `7cbd2f215582e84bc8cb2656b9d958b3188f9f80` | Emery-only metadata, legible button navigation, host tests |
| [pebble-pietas](https://github.com/ewijaya/pebble-pietas) | `46a7cafcc187bb4c015e81dddc6f5f1d38ef84fc` | Offline native app, explicit data scope, versioned bounded persistence |

The local Orationes screenshot script had uncommitted changes; they were left
untouched. The additional nearby daily-mass repository also had existing work
and was not changed. No reference-project source was reused. Public documentation
and firmware source remain under their upstream licenses.

During the first emulator attempt, isolating XDG alone proved insufficient:
the CLI's global `/tmp/pb-emulator.json` caused it to replace the pre-existing
emulator process when enabling VNC. Its original flash image was unchanged,
and that Emery session was restarted. The final adapter additionally isolates
`TMPDIR` and Python's cached temporary path; subsequent Inspect launches and
shutdowns leave the restored global emulator running.

[toolchain-lock.json](toolchain-lock.json) records hashes of the actual SDK
header/library, emulator firmware image and executables. Release
`artifacts/build-manifest.json` binds the PBW checksum to application inputs.
ZIP timestamps can change archive bytes on rebuild; reproducible setup means
the same pinned compiler/API inputs and behavior, not a claimed bit-identical ZIP.
