# Validation record

Tested on 2026-10-21 on the audited x86_64 Ubuntu server. After the initial
milestone, Edward explicitly requested installation and enabled Dev Connection.
Cloud ping and installation of the checksum-verified v0.1 PBW succeeded on his
PT2. This does not complete the broader physical checks below.
The [physical watch capture](../artifacts/pt2-installed.png) was inspected and
shows Inspect's Overview open, scrolled to its foreground-scope explanation,
with 90% reported battery. See the [installation record](../artifacts/physical-install.json)
for the exact artifact checksum. No physical clock or simulated input was changed.
The evidence below records local validation. Remote build results are available
separately in [GitHub Actions](https://github.com/ewijaya/pebble-inspect/actions).

The larger-font v0.1.1 update also passed the full host/emulator checks and
installed successfully through the existing cloud developer connection on
Edward's request. Memory now shows used percentage and used/free/total KiB for
Inspect's heap. The UUID and persistence schema are unchanged.
The [v0.1.1 physical capture](../artifacts/pt2-0.1.1-installed.png) shows the
larger text on the scrolled Overview; the [install record](../artifacts/physical-install-0.1.1.json)
identifies the exact installed bundle and checksum.

## Automated host checks

`make test` compiles the actual pure model, codec, persistence adapter and
formatters with GCC warnings as errors plus AddressSanitizer and
UndefinedBehaviorSanitizer. The final clean `make` log is retained in
[`artifacts/build-and-test.txt`](../artifacts/build-and-test.txt).

Passed cases include:

- Battery: insufficient evidence, flat readings, exactly qualifying thresholds,
  119-minute rejection, charging/plugged state, rising/noisy percentages,
  discontinuous sessions, backward/large-forward clock moves, duplicate
  timestamps, stale 300/301-second boundary, excessive forecast rejection,
  integer extremes and counter/segment wrap.
- Connection: initial state without a fictitious transition, real drops and
  reconnects, seconds assigned to reported state only in trusted intervals,
  foreground start/stop, unknown-end recovery, gaps and saturating totals.
- Codec: round trips including 4,000 generated histories; every single-byte
  corruption and truncation; unsupported schema, counts, flags, percentages,
  event kinds and inconsistent times, including malformed values with a
  recomputed CRC. Failed decoding leaves the destination unchanged.
- Actual storage adapter with fake SDK storage: both banks, generation wrap,
  missing/corrupt/short values, 144 interrupted/failed/short/corrupt write and
  readback cases across zero/one/two committed banks, recovery from older
  history, unsupported schema, first-save orphan chunks, and persistent
  recovery/error notices across save and retry.
- Formatting: duration units and boundaries, UINT32_MAX, timestamps, small
  buffers, clock reversal and stale age. Unsupported or insufficient estimates
  never render their numeric zero fields as a valid forecast.
- Heap summary: zero-total unavailability, 0% and 100%, fractional rounding,
  uint32 limits, overflowing-total rejection, KiB conversion and small buffers.
  The v0.1.1 information font is 24-point (previously 18); footer is 18-point.

## Native build

Clean command: `pebble clean --sdk 4.33.1 && make`.
Pebble Tool 5.0.40 / SDK 4.33.1 built the **emery-only** native app.
The PBW ZIP integrity, name, UUID, version, platform, native binary and absence
of worker/phone JavaScript were verified. The input and artifact hashes are in
[`build-manifest.json`](../artifacts/build-manifest.json).

| Quantity | Result | Scope |
| --- | ---: | --- |
| PBW archive | 22,412 bytes | Compressed install artifact (v0.1.1) |
| Resource pack | 4,263 bytes | Linked app resources including SDK pack overhead |
| RAM footprint | 24,857 bytes | SDK's app code/static-data footprint, out of 128 KiB app budget |
| Available app heap before runtime allocations | 106,215 bytes | Link report; **not free whole-watch RAM** |

The SDK's linker emitted its RWX LOAD-segment warning; compilation and bundling
succeeded. This watchapp uses the SDK's standard linker template; no system
compiler, glibc, firmware or linker script was replaced to suppress it.

## Emulator procedure and evidence

The headless emulator uses SDK 4.33.1's QEMU 10.1.5-pebble14 and public firmware
version **4.33.2**, reporting modern PT2 model ID **11**. The installed physical
watch firmware remains unverified. Commands:

```sh
make emulator
~/.local/share/uv/tools/pebble-tool/bin/python scripts/emulator_smoke.py
~/.local/share/uv/tools/pebble-tool/bin/python scripts/emulator.py kill
```

The adapter supplies documented `--vnc` and isolates data **and** temporary
process registry. QEMU debug ports, VNC, websockify and the simulated-phone
bridge were checked with `ss` and bound to loopback. See the environment audit
for the first-attempt registry issue and its correction.

The smoke script exercises all six screens and the screen menu, scrolls each
detail to both boundaries, injects emulator charging/disconnection/reconnection
events, exits with actual Back buttons, queries running-app state to verify the
exit, and reopens repeatedly. It waits for launch animations before pressing
buttons. It also repeats navigation inside one running process and captures
heap logs. Different pages/animations have different transient heap allocations;
the test compares equivalent checkpoints and checks the firmware's exit report
for remaining allocations.

The first short disconnection injection remained connected because the public
service filters outages. The inspected firmware uses a 25-second disconnect
debounce; the final test holds the simulated outage for 30 seconds before
capturing it. The app's event history is explicitly scoped to reported SDK
state, not raw radio events.

Screenshots in [`docs/screenshots/`](screenshots/) are raw 200×228 emulator
captures. They were inspected for readable scope/age, text wrapping, available
buttons, fixed headings and scroll reachability. The dot-pattern edges are the
native ScrollLayer's scroll affordance. Long text intentionally scrolls; it is
not squeezed into unreadable fonts. Initial inspection led to a shorter
Overview, visible battery status and a menu Back-handler fix.

Final automated emulator results and scoped heap figures are recorded in
[`artifacts/emulator-smoke.json`](../artifacts/emulator-smoke.json). All nine
entry checkpoints used 1,352 heap bytes. Six equivalent navigation checkpoints
held at 1,944 bytes; all nine firmware exit reports showed 0 bytes still
allocated. No Inspect warning/error log was observed. The 30-second simulated
outage produced disconnected states, one reported drop, and reconnection;
reopening preserved session/gap counts. These are Inspect/emulator figures,
not whole-watch RAM or a physical radio reliability measurement. The test
inputs live only in the host script. The production C app contains no fixtures
or demo mode. The emulator's battery percentages and connection transitions are
simulated; no battery-drain, radio-reliability or sensor-accuracy claim follows.
Estimate threshold/stale/corruption cases are host-tested; a real multi-hour
discharge forecast and device storage-failure behavior remain hardware checks.

## Physical PT2 checklist — pending

1. Install the checksum-verified stock PBW using [install.md](install.md).
   Confirm the modern PT2 model and actual firmware version against System →
   Information. Use all four buttons on every page, including Back to exit.
2. Compare reported percentage and charger states with the watch UI while
   unplugged, charging, and fully charged but still plugged. Test readability
   in daylight and with ordinary backlight timeout.
3. While Inspect is foreground, disconnect/reconnect the phone and compare
   both public connection states to the phone app. Check one drop per observed
   transition. Close Inspect during an outage and reopen: the absent interval
   must remain a gap, with no invented drop or duration.
4. Test focus loss from notifications/overlays and return. Session boundaries
   must prevent a continuous-estimate claim across the unobserved period.
5. Keep Inspect foreground long enough for qualifying actual discharge
   observations. Compare the rough forecast against later use, noting that
   Inspect replaces the watchface and changes workload. Charge or raise the
   reported percentage: the former forecast must disappear/reset.
6. Move the clock backward and substantially forward; verify discontinuities
   and no absurd duration/forecast. Small forward changes inside 300 seconds
   are an acknowledged wall-clock limitation, not verified monotonic timing.
7. Reopen repeatedly and update using the same UUID. Verify history retention,
   gaps and sampled heap stability. Unexpected termination can lose unsaved
   observations and must not be labeled a watch reboot.
8. Measure observer power overhead against an equivalent baseline on hardware
   before making any battery-efficiency claims. CPU/subsystem energy, flash,
   system memory and global activity remain unavailable in this stock build.

The requested implementation agents ran as `gpt-6.1-sol` with high reasoning.
The running orchestrator could not switch or verify its own model as
`gpt-6-astra`; that limitation was disclosed before implementation.
