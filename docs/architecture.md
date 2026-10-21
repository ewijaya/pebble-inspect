# Architecture

`main.c` owns the process lifecycle. It subscribes to battery, connection,
minute-tick and app-focus services, obtains an initial public-API snapshot,
and stops observations on focus loss. It unsubscribes on exit. No worker,
wakeup schedule, AppMessage, network, sensor subscription or forced backlight.

| Layer | Files | Responsibility |
| --- | --- | --- |
| Public stock provider | `provider.c/h` | Battery, distinct phone/PebbleKit state, Inspect heap, WatchInfo; timestamped snapshot |
| Pure model | `model.c/h` | Bounded actual samples/events, foreground intervals, session/gap markers, saturating counters, estimates with explicit status |
| Portable codec | `codec.c/h` | Version-1 little-endian records, CRC32, structural/semantic validation, atomic decoding |
| Persistent adapter | `storage.c/h` | Two alternating banks, chunks ≤256 B, generation/length/blob and manifest CRC, checked writes/readback, commit manifest last |
| UI and format | `ui.c/h`, `ui_format.c/h` | One window, reusable layers, six scrollable pages, bounded strings and tested units/age formatting |
| Capability policy | `capabilities.c/h` | Stock-build limits; no private calls or guessed statistics |

The model retains 48 battery observations and 24 events, evicting oldest first.
Minute peeks establish continuity; history stores 15-minute observations or
changed state. Each session, charging transition, rise or time discontinuity
starts a new discharge segment. Duplicate timestamps add no duration and do
not satisfy the estimate's distinct-timestamp evidence rule. Connection duration
belongs to the previous reported state up to the next observed event; no
interval above 300 seconds or across sessions is counted. Totals are bounded
uint32 seconds/counts and saturate rather than wrap. Timestamps use uint32
epoch seconds; backward/wrapped time creates a discontinuity.

Estimates have `insufficient`, `ready`, `charging`, `stale`, and `unsupported`
statuses. Only ready values render as time. Byte values in the provider are
always explicitly Inspect-scoped; unavailable whole-watch metrics have no
numeric placeholders. A future provider may replace collection without changing
the pure accounting model, but adding system scopes needs a separately defined
snapshot contract, as described in the firmware roadmap.

Persistence serializes at most **680 bytes** of payload. Two banks plus their
20-byte manifests use at most **1,400 logical bytes** in SDK persistence (keys
7100–7104 and 7116–7120 are reserved; currently three chunks suffice). This is
an app payload budget, not actual flash allocation. Ordinary writes batch at
15 minutes; session starts/stops and exit checkpoint immediately. A partial
save leaves the prior complete bank recoverable. If both banks are corrupt or
unknown-schema, they are rejected and a visible invalid-history status starts
an empty model; the next save replaces one bank. There is no migration from
future unknown schemas. Uninstalling the app removes its saved history.

After an unclean termination, loading an active checkpoint adds “Last seen;
end unknown” at the last observation. Time since that checkpoint is never
credited. Since samples are batched, up to 15 minutes of the latest unsaved
observations can be lost. These are diagnostic observations, not durable
whole-watch logs.

No dynamically sized data structure grows during navigation. The model,
storage scratch buffer and UI strings are static; checked SDK allocations
create the window/layers once. The runtime heap screen covers allocations
reported by the public heap APIs; the build RAM footprint separately includes
linked code and static buffers.

v0.1.1 derives Inspect heap total and used percentage with 64-bit intermediate
arithmetic, rejecting zero/overflowing totals. Display uses one-decimal KiB and
percent; exact used/free byte counts remain available. Information text is
24-point, with content measured at that same font size before setting scroll
bounds. Larger text changes presentation only; history format and UUID remain
unchanged.
