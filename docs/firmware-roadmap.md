# Firmware diagnostics roadmap

This is a design and source audit, not an implemented firmware feature. The v0.1 stock watchapp remains useful with public SDK 4.33.1 battery, connection, Inspect-only heap and WatchInfo APIs, plus a conservative remaining-runtime estimate over qualifying foreground observations. No firmware has been modified, built, installed or flashed for this work. All proposed API names, versions, permission policy and limits below are provisional; no minimum supporting firmware release is established.

The source baseline is [`coredevices/PebbleOS` at `5cf6c353bad2903e0a789b27bce278f54317b267`](https://github.com/coredevices/PebbleOS/commit/5cf6c353bad2903e0a789b27bce278f54317b267), inspected 2026-10-21. Modern Core Devices PT2 is `obelix` / `emery` / SF32LB52. Current `kernel/` is the native PBL scheduler; it replaced the historical FreeRTOS shim. See the [capability audit](capabilities.md) for ranked scope, released-SDK checks and exact source references. The user's installed firmware build remains unverified.

## Two viable routes

| Route | Advantages | Costs and limits | Recommended use |
| --- | --- | --- | --- |
| **Built-in firmware Diagnostics view**, beside Settings > System > Information | Can use internal APIs with existing privilege; no public trampoline/SDK ABI change; fits the existing real uptime and firmware identity view. | Requires a firmware release, firmware UI code and maintenance. Installed apps cannot consume it automatically. Opening the view still replaces the foreground watchface. | First prototype and first upstream discussion, especially a single kernel-heap or PFS-space page. |
| **Read-only exported diagnostics snapshot API**, consumed by an experimental Inspect build | Reusable by third-party diagnostics tools; watchapp owns presentation; bounded copied data isolates internal object lifetime. | Requires wrapper/syscall validation, export whitelist and ABI revision, matching SDK release, permissions and compatibility policy. Cross-app data needs privacy treatment. | After owners agree the metric contract and shipping/compatibility policy. Start with aggregate kernel heap only. |

Do not bypass the SDK using fixed addresses, private function-table indices, copied internal headers, debugger memory reads from the app, or an invented AppMessage endpoint. Source availability grants neither app privilege nor a stable ABI.

## Smallest useful first metric: kernel allocator snapshot

Choose **kernel heap capacity, currently allocated bytes, free bytes and largest free block**. These explain allocation pressure and fragmentation without enumerating apps, files, threads, health readings or network identifiers. Label the page **Kernel heap**, not “free system RAM.” Physical RAM includes other regions, stacks, buffers and reservations.

The [kernel heap already has a protected `Heap` instance and getter][kernel-heap]. [The allocator exposes size, headroom and totals][heap-api]. `Heap.current_size` includes allocation block headers; [`heap_calc_totals()` walks whole allocator blocks][heap-impl] to produce used/free/largest-free values. Those whole-block totals include metadata, and largest free block is not a guarantee that an equal-size user allocation succeeds. Preserve that accounting convention in the contract. Do not obtain “free” by an unrelated memory-map subtraction or probe the heap by allocating until failure.

The first firmware patch should:

1. Add a firmware-local, read-only helper that returns a fixed-size value snapshot under the allocator's existing lock discipline. Start with one domain and one set of scalar values. The inspected `heap_calc_totals()` acquires/releases the heap lock itself, and its internal scan emits a diagnostic serial line. Factor or reuse a non-logging scan path; do not acquire the same non-reentrant lock twice or emit a log on every read.
2. Include a sample's boot-relative timestamp, capacity, allocated/free/largest-free bytes, availability/error status and an explicit allocator-accounting identifier. Do not reset high-water state on read. The existing analytics collector resets its high-water baseline, so a future “minimum free since boot” metric needs its own promised lifetime or must say “since analytics reset.”
3. Add one built-in Diagnostics view using the helper, refreshing only on entry and explicit user action. It must be usable with zeros and unavailable/error values without treating one as the other.
4. Measure snapshot latency and allocator lock hold time on the target configuration and emulator. If scanning makes interrupts unavailable too long, initially ship only cheap protected counters or schedule/copy a cached scan outside the app's syscall path. Publish the sample age.
5. After this contract is reviewed, optionally expose the same helper through the bounded snapshot API below. That would be the experimental Inspect build's first additional metric. The stock PBW must not gain a reference to the new symbol.

Usable flash is a similarly useful alternative, but its first patch needs a PFS-specific consistency review. [`get_available_pfs_space()`][pfs-space] loops PFS page flags without taking its own mutex and budgets only 80% of PFS space for wear leveling. A new snapshot must run under the PFS owner's correct locking discipline or use a consistent cached counter; adding an arbitrary external mutex does not protect the underlying pages. Expose **partition bytes**, **policy budget bytes**, **allocated page payload bytes**, **available budget bytes**, reserve policy and validity separately. Do not rename the existing result “raw free flash.”

## Proposed bounded snapshot contract

This is a contract sketch, not a header to copy into the current app:

```c
// Future matched firmware/SDK only; provisional spelling.
status_t diagnostics_snapshot_get(uint16_t requested_version,
                                  uint32_t requested_domains,
                                  void *output, size_t output_capacity);
```

Version 1 should fit a small fixed maximum response, proposed **128 bytes**, for aggregate kernel-heap data plus a header. No unbounded malloc, user pointer retention, callback, iterator or variable lifetime object should cross the boundary. Larger future task or storage snapshots need their own bounded version/records/pagination contract rather than silently growing v1.

The response header should contain:

| Field | Meaning |
| --- | --- |
| `abi_version`, `header_size`, `bytes_written` | Version and exactly how much caller-owned memory is valid; fixed-width fields and explicit alignment/serialization. No implicit host pointer-size ABI. |
| `supported_domains`, `valid_domains` | Capability discovery and per-call availability are separate. Firmware can support a domain whose current sample failed. |
| `status`, `flags` | Unsupported version/domain, permission denied, short buffer, temporarily busy, partial, stale or truncated. These must never masquerade as a measured zero. |
| `boot_id`, `sample_uptime_ms`, `generation` | Distinguish reboot, identify snapshot time, and detect replacements/refreshes. Prefer a 64-bit boot-relative clock and define wrap behavior. A proposed boot identifier is not a claim that today's stock firmware exports one. |
| `domain_status`, `sample_age_ms` | Domain-specific collection/error/freshness, because PFS, allocator and sensors may not be sampled atomically together. |

Use only requested domains, reject unknown required flags, and return a version/required-size error without overrunning a short buffer. Validate caller output address/range, multiplication/addition overflow, capacity and alignment in the syscall layer. Build a firmware-owned fixed-size temporary snapshot while locks are held, release locks, then copy validated scalar data to app memory. No direct exposure of `Heap *`, thread objects, addresses, function PCs, kernel stack contents, filenames, UUID pointers or live `const char *` values.

Lock only one subsystem at a time; use a documented lock order if multiple locks become unavoidable. No I/O, flash writes, allocation, sensor enabling or application callback while a scheduler/heap lock is held. Separate timestamps permit honest non-atomic domain collection. A bounded response length alone does not bound collection cost: filesystem page walks, heap block walks and stack fill scans need separate time budgets.

An initial permission proposal is user-visible opt-in diagnostics for app access, with aggregate kernel/PFS counters allowed and all cross-app identities omitted. This is a new policy decision, not an existing manifest permission. System Diagnostics may read its own internal helper. A background third-party monitoring worker is not passive: the pinned [launch syscall][worker-switch] asks to replace an existing different worker, and the [manager][worker-manager] closes that worker before starting the next. There is one third-party background worker slot; do not replace the user's worker merely to collect diagnostics.

A future cross-app mode should require an explicitly granted diagnostic scope; redact health sensor values, Bluetooth addresses, app-private persistence and crash addresses. Read-only access still reveals user activity when combined over time.

Proposed collection rate: at most **one full snapshot per second**, shared/coalesced across clients, with explicit manual refresh and a stale-age indicator. Validate the chosen limit through timing and power tests rather than treating it as established. Read requests must not write persistent logs, wake sensors, force a PMIC conversion or maintain a hidden worker. An optional asynchronous cache can avoid expensive synchronous app syscalls; document its update cadence and owner.

## ABI and older firmware

The [official SDK export guide](https://developer.repebble.com/pebbleos/docs/development/sdk_export/) requires implementation, syscall/wrapper, [`exported_symbols.json`][exports] registration with a new `addedRevision`, and the [process SDK-version ledger][sdk-ledger] update. Current pinned source uses `PBL_SYSCALL`; its details take precedence over older macro examples in imported documentation. Add generated app headers and `libpebble.a` trampolines by building the corresponding SDK, and test them. Changing only a header or symbol whitelist is incomplete.

A new trampoline indexes the firmware's compiled function table. Calling it on firmware that predates the export can crash the app. **`PBL_API_EXISTS()` checks the build SDK, not the installed watch firmware.** An in-function `UNSUPPORTED` return is possible only when the export already exists; it cannot make a call to a missing trampoline safe.

Maintain two explicit deliverables:

- **Stock Inspect**: existing SDK 4.33.1 APIs only. No new diagnostics declaration, link reference, dynamic address probing or runtime fallback call. Continue displaying public metrics regardless of experimental work.
- **Experimental Inspect**: separate build configuration and PBW identity, compiled against the matching diagnostics SDK and declared minimum app/firmware ABI. Install only on a verified supporting firmware build; use the platform's actual loader compatibility rules and verify rejection on an older firmware emulator before distribution. The exact minimum release number is assigned when the API ships, not inferred from current source.

The new export can carry its own domain/version discovery for future compatible firmware after that initial ABI gate. If maintainers require one PBW for both old and new firmware, design a capability transport that is already safe and supported on the baseline, or first ship a baseline discovery export in firmware and wait for that baseline. A stock PBW must never “test” a missing export by calling it.

## Later phases, in useful order

| Phase | Concrete extension | Required instrumentation / caveat | Evidence baseline |
| --- | --- | --- | --- |
| **1** | Aggregate kernel heap; optionally usable PFS space | Existing protected allocator counters; define page/reserve accounting and bounded scan time. No “all RAM” claim. | [Kernel heap][kernel-heap], [heap totals][heap-api], [PFS accounting][pfs-space]. |
| **2** | OS uptime and last reset category | Existing uptime and retained reason; copy category only. Define boot identity, retention and missing/corrupt reason. A reset cause does not prove a faulty third-party app, and a retained slot is not history. | [System Information][uptime], [reset storage][reset]. |
| **3** | Native thread activity / CPU occupancy | Existing snapshot and analytics, stable generation, interval delta, truncation and loss reporting; repair current-thread accounting if required before exact intervals. | [Snapshot][thread-snapshot], [contract][thread-header], [scheduler][scheduler], [task analytics][task-analytics]. |
| **4** | App storage breakdown | Join installed metadata to executable/resource/worker/persist files, count pages and shared/unattributed storage, ownership-safe bounded iteration. Never sum payload bytes and call that flash allocation. | [PFS list API][file-list], [app file names][app-file], [persistence][persist]. |
| **5** | Global sensor, worker and scheduled-launch diagnostics | Snapshot managers without subscribing/enabling sensors. Separate requested versus effective Hz, active duration, global worker identity and scheduled app-launch deadlines. Cross-app identities need permission. | [Accel manager][accel], [HRM manager][hrm], [wakeup service][wakeup], [worker manager][worker-manager]. |
| **6** | Wakeup/idle activity and energy estimates | New cumulative suspend/resume reasons, interrupt/timer counters, display/radio/sensor on-time, baseline/calibrated current model, lifecycle changes and shared attribution. Benchmark diagnostics overhead. | [Native idle accounting][scheduler], [whole-battery PMIC measurement][pmic]. |

### Native PBL task accounting needs careful adaptation

The current source already has `pbl_thread_foreach()`, `pbl_thread_count()`, and `pbl_thread_stats_snapshot()` in [`kernel/debug.c`][thread-snapshot]. The statistics snapshot walks the live thread list while interrupts are locked and includes pointer/name, sequence number, state, runtime ticks and stack high-water data. Do not serialize that internal struct directly: its pointer and name lifetimes are unsafe outside the snapshot lock, and it exposes private addresses. Copy a bounded name while protected, or a stable public task category, and a non-pointer identity/generation into fixed storage. Return counts plus a truncation flag; max output capacity alone does not tell the caller that threads were omitted.

At this revision `run_time` accrues on switches between different threads, in [`sched_switch_in()`][scheduler]. The snapshot copies that accumulator without adding the currently running thread's not-yet-switched-out slice. A future interval API must define whether it includes that slice and fix/normalize it in the firmware collection path before advertising exact totals. Runtime fields and the snapshot denominator are 32-bit; handle modular subtraction only across a bounded interval, and detect reboot/long gaps. Default SF32LB52 tick frequency is 1000 Hz, but export the actual frequency rather than assuming a millisecond on every board.

The “stack high-water” value here is **bytes never touched since thread creation**, not current unused stack or used stack. Scanning stack patterns costs time proportional to unused stack, currently within the interrupt-locked snapshot. Omit those scans from the first API or collect them separately with a measured budget. Existing per-PebbleTask analytics accumulates dead-thread runtimes and maps kernel categories; use it as source evidence and a design input, not an assumption that task pointer == app identity. Current foreground app identity can change on the same logical task.

CPU occupancy derives from cumulative runtime deltas divided by elapsed ticks. Account for idle/sleep conventions, interrupt work charged to interrupted threads, task creation/death, and clock/tick quantization. Report the observed interval and missing/unattributed share. CPU runtime or occupancy cannot be converted into per-app energy without a model and calibration.

### Energy is a separate research project

The PT2 configuration enables the nPM1300 PMIC, and [`battery_get_constants()`][pmic] obtains battery voltage/current/temperature with waits and failure/timeout paths. This is a whole-battery reading, not a cross-app power meter. Charger state changes both sign/interpretation and the battery's relationship to system load; validate hardware and conversion behavior before exporting values. Cached samples need age and error status, and polling the PMIC is itself work.

For future estimated subsystem energy, record low-overhead continuous on-time/activity counters in firmware while the previous watchface actually runs. Calibrate against external power instrumentation across display, radio, backlight, sensors, speaker/mic, CPU/sleep and charging states. Identify shared costs and simultaneous consumers, label modeled allocations **estimated**, and expose an unattributed bucket. Battery percent delta can validate a long whole-watch trend but cannot establish precise instantaneous watts or blame an individual watchface. Stock Inspect cannot recover that evidence after replacing the watchface.

## Validation before proposing a release

First submit a small firmware-side helper and one Diagnostics view or one aggregate snapshot, with tests for unsupported versions/domains, short/invalid buffers, permission failure, zero/empty states, partial/stale output and lock safety. Exercise repeated reads, concurrent allocations, malformed output pointers and collection timing. Verify no retained pointers, no per-read persistent writes, no sensor activation and no app-callable control path.

For task/storage extensions, test creation/death/uninstall mid-collection, truncation, duplicate/reused identities, counter wrap/reboot, shared-file ownership, reserve policy and interruption. Compare counters to existing internal diagnostics without claiming that agreement proves physical accuracy. Measure observer overhead with diagnostics off/on and at candidate refresh rates.

Build matching firmware and SDK in an isolated environment, run the firmware's prescribed tests and QEMU `qemu_emery` first, then build the experimental PBW against that SDK. Demonstrate old app on new firmware and explicit experimental-app rejection on old firmware. Document exact firmware revision, SDK build, ABI revision, PBW identity and units. **This project has performed only the source audit and stock-app work; no firmware patch or flashing is authorized by this roadmap.**

## Pinned implementation references

[kernel-heap]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/kernel/kernel_heap.c#L18-L83
[heap-impl]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/lib/util/heap.c#L150-L194
[heap-api]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/include/pbl/util/heap.h#L193-L218
[pfs-space]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/services/filesystem/pfs.c#L2084-L2103
[exports]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/tools/generate_native_sdk/exported_symbols.json
[sdk-ledger]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/process_management/pebble_process_info.h#L83-L171
[uptime]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/apps/system/settings/system.c#L219-L227
[reset]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/system/reboot_reason.c#L98-L136
[thread-snapshot]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/kernel/debug.c#L10-L70
[thread-header]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/include/pbl/kernel/debug.h#L119-L173
[scheduler]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/kernel/sched.c#L315-L364
[task-analytics]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/kernel/pebble_tasks.c#L238-L285
[file-list]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/include/pbl/services/filesystem/pfs.h#L309-L326
[app-file]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/include/pbl/services/filesystem/app_file.h#L15-L36
[persist]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/services/persist/service.c
[accel]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/include/pbl/services/accel_manager.h#L15-L95
[hrm]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/include/pbl/services/hrm/hrm_manager.h#L156-L231
[wakeup]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/services/wakeup/service.c#L589-L620
[worker-manager]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/process_management/worker_manager.c
[pmic]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/drivers/pmic/npm1300.c#L532-L637

[worker-switch]: https://github.com/coredevices/PebbleOS/blob/5cf6c353bad2903e0a789b27bce278f54317b267/fw/syscall/app_worker_syscalls.c#L33-L56
