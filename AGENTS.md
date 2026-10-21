# Inspect project rules

Native C watchapp, display name Inspect, package pebble-inspect, v0.1.1.
UUID `2de5b9f0-ee00-4a27-9474-5aacd231c6ac` is permanent.
Only target `emery`: current Core Devices Pebble Time 2, 200×228.
Use 24-point system font for information text and 18-point button hints.
Preserve readable scrolling rather than shrinking information to fit.
Heap percentage uses Inspect's used / (used + free); sizes use KiB (1024 B).

Development is paused at Edward's request, pending supported PT2/PebbleOS
system-diagnostics exports. Do not resume feature or firmware development
without Edward's instruction. Preserve the v0.1.1 artifacts and roadmap.

Read README.md, docs/capabilities.md and package.json before changing metrics.
No synthetic production readings. Label heap as Inspect heap; persistence
allowance is not free flash; connection is not internet/notification health;
app launches are not watch restarts. Unavailable, stale and insufficient are
distinct from zero. Any demo build needs a persistent DEMO label.

Foreground observations only. Preserve explicit sessions, gaps and unknown
termination. Never bridge a gap for an estimate or connection accounting.
Estimate evidence and invalidation rules live in model.c and its tests.
Clock discontinuities must invalidate affected intervals. The phone connection
state is the SDK's documented state, not a raw radio or service health probe.

Keep provider, pure model/codec, persistence and UI separate. Fixed-size
histories; version persistence changes; check every read/write and allocation.
No worker, phone companion, backend, telemetry or network dependency by default.
Do not add new firmware trampolines to the stock target. Firmware experiments
need a separate target and compatible firmware/SDK pair.

Verified tools: Pebble Tool 5.0.40, SDK 4.33.1, Python3.10, x86_64 Linux.
`sdkVersion: "3"` is metadata format, not the actual SDK version.

```sh
make test             # host tests
make build            # pinned Emery build + PBW metadata/checksum audit
make                  # both
make emulator         # isolated localhost-only headless Emery install
make screenshots      # current screen; inspect it visually
```

Read docs/setup.md for the emulator adapter. Use its Python from the CLI uv
environment. Do not use global `pebble kill` or alter somebody else's emulator.
Avoid dumping process command lines or Waf lock files (may contain credentials).
Run `pebble clean --sdk 4.33.1` if metadata/message keys change.

Tests use GCC, never a shell `cc` alias. User-approved agent work must have
explicit nonoverlapping file ownership; this project does not require agents
for ordinary changes. No pushes, public repository creation, app publication,
physical watch installs, recovery/bootloader changes, or cloud resources unless
Edward requests them. Documentation must distinguish host/emulator/hardware
evidence and record exact source revisions for firmware claims.
