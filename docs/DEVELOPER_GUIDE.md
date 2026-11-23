# Developer guide

Start with this file, then use [architecture](architecture.md) for timing, data
schemas and memory budgets and [known issues](KNOWN_ISSUES.md) for accepted risks.
The source remains the behavior authority. This guide describes the current
M7 application; it does not promise hard real-time sampling or storage durability.

## Where execution begins

Arduino invokes `setup` in `src/main.cpp`, which configures Serial, Wire, RPM
pins/ISR and DallasTemperature, then video/touch and the EEZ UI. Successful video
initialization installs the project display callback. UI initialization precedes
application-thread dispatch; library-owned threads may already exist. Heap
observation is installed before the application logger starts. USB callbacks
publish attachment events; they do not mount the filesystem. The background
Arduino loop only sleeps. There is no coordinated shutdown API: tasks run for
boot lifetime and handle failures through their own invalidation/retry policies.

Follow the declarations and ownership comments in main.cpp before changing
startup order. READY means dispatch attempted, not a GNSS fix or successful mount.
Only selected initialization results are checked; do not infer stronger readiness.

## Source map

| Files under src | Responsibility / safe extension boundary |
| --- | --- |
| main.cpp | Hardware wiring, task dispatch, RPM/temperature tasks, FILE adapter and logger loop |
| gnss_service.h/.cpp | Sole receiver owner, latest snapshot and locked FIFO service interfaces |
| gnss_epoch.h | Pure freshness, re-anchoring and rate qualification policies |
| gnss_log_fifo.h | Fixed records, reservation/accounting, segment search and injectable checked writes |
| log_clock.h / local_time.h | Legacy one-time UTC anchor versus UI CET/CEST conversion |
| rpm_display_filter.h | Display-only smoothing, separate from raw RPM and warning |
| vars.h/.cpp / ui_self_test.h | Handwritten EEZ bindings and startup frame generation |
| display_rotation.h/.cpp | LVGL callback registration, coordinate mapping and checked HAL adapter |
| display_rotation_buffer.h / display_dma_contract.h | Pure extent/allocation/flush and DMA lifetime policies |
| usb_mount_state.h / usb_io_diag.h | FAT registration recovery and first-failure/output-budget accounting |
| heap_diag.h/.cpp | Bounded heap snapshots and allocation-failure evidence, not leak attribution |
| diagnostics_config.h | Compile-time levels and project-prefixed subsystem selectors |
| ui/* | Generated EEZ interfaces, widgets, assets and fonts; regenerate rather than hand-edit |

Pure headers are exercised by host tests with injected operations. Hardware
adapters deliberately stay separate; a policy fake does not emulate bus traffic.

## Trace a measurement

RPM edges update two volatile timestamps in the ISR. The RPM task copies the pair
in a short critical section, computes RPM, drives pin 2 and publishes RPM/warning
under dataMutex. Temperature publishes Celsius under that same mutex. Display
and logger take short copies, release locks and independently format their outputs.
Only the display uses the adaptive RPM filter; warning and persistent raw RPM do not.

The GNSS task qualifies PVT epochs, publishes a latest-value snapshot under
snapshotMutex, releases it, then admits a raw-unit record under fifoMutex.
Display/legacy logger read latest snapshots; historical GNSS logging drains the
separate queue. Reconnect invalidates presentation data without erasing queued
records. Invalid fix/time flags govern field interpretation, not FIFO admission.

The logger reserves at most 20 entries into its static work buffer, releases the
FIFO lock, formats/writes/closes synchronously, then completes the exact reservation.
Reserved entries remain queued; a full producer drops the newest arrival and
increments overflow. Every attempted failed block becomes uncertain without replay.
Unattempted entries survive. Keep the accounting identity
`accepted = written + queued + overflow + uncertain` under the FIFO lock.

## Concurrency and ownership rules

- GNSS owns Wire receiver/library state; temperature owns its OneWire sensor.
- Logger owns all application connect/mount/unmount/FILE calls and shared line/path
  scratch. Do not pass its format-buffer pointer to deferred work.
- Display owns LVGL, model buffers and rotation staging after setup. EEZ string
  getters return borrowed static storage; they do not transfer ownership.
- ISR and USB callbacks must not print, allocate, acquire RTOS mutexes or perform
  filesystem operations. Heap callbacks record bounded evidence only.
- Never nest snapshotMutex and fifoMutex or hold either across I/O, formatting,
  allocation, rendering or sleeps. Pure policy classes provide no internal locks.
- Atomic USB flags/generation are individual observations, not one atomic snapshot.
  They invalidate confirmations but cannot protect USBHostMSD internal pointers.

Timing constants are scheduling intentions plus sleeps, not measured deadlines.
A synchronous temperature conversion, I2C/USB command, HAL poll or Serial call may
consume additional wall time. Read the architecture before budgeting a new task.

## Modify the UI safely

Edit `mofa.eez-project` in EEZ Studio, regenerate versioned `src/ui`, then review
both project and generated changes. Keep handwritten bindings in vars.cpp and
application-facing setters in vars.h. Check the digit/font/sentinel conventions
before adding unavailable-value text. Update startup self-test coverage for new
indicators. LVGL features disabled in platformio.ini may need deliberate re-enabling
for new widgets/styles; that is a separate functional/configuration change.

Only partial RGB565 is supported by the project flush. Physical DSI dimensions
and LVGL rotated offsets are not interchangeable with logical display dimensions.
Keep bounds/stride/overflow checks before copying. Even zero rotation uses owned
staging so LVGL can reclaim its buffer after ready. A failed DMA abort/inactivity
proof quarantines staging until reboot; do not free, grow or modify pinned storage.
Every flush path must complete LVGL exactly once. Validate visible output on target.

## Add a sensor

Choose a hardware owner and document voltage/pin/bus and blocking behavior first.
Avoid adding an independent Wire user without an explicit bus-ownership design.
Publish a value plus validity/freshness metadata under a short lock or a value
snapshot; define units, sentinel policy and timeout before connecting UI/logging.
Do not call slow conversions from the RPM ISR, display flush or a shared-data lock.
Extend the handwritten UI interface and pure formatting/status tests before any
EEZ regeneration. Recheck scheduling and stack/heap watermarks on hardware.

## Extend persistent logging

First decide whether data represents current-value sampling or historical epochs.
Do not feed drained historical GNSS times back into the live legacy clock. Keep
raw units/validity until formatting. A changed file schema needs explicit versioning
and consumer/test updates; do not silently append columns to legacy files.

Use the logger owner and checked-write policy: full byte count plus ferror, flush,
close and unchanged medium are all required. Do not reuse FILE after close. Keep
first-failure context through cleanup. Allocate new segment names exclusively;
never append behind an uncertain partial row. Bound RAM/candidate searches and
specify overflow/no-replay accounting. Physical durability requires evidence beyond
successful libc calls. New sensor streams must not accidentally prevent legacy
writes or consume GNSS reservations without completion.

FAT registration is distinct from usability. A failed mount may still require
cleanup; pending registration blocks remount/writes. Callbacks only notify the
logger. Preserve accepted USB detach/cancellation risks and do not claim this
application state machine proves driver lifetime or controller quiescence.

## Diagnostics, build and tests

See [README](../README.md) for exact compiler flags, project-local PlatformIO
commands and tests. Levels 0..4 select RELEASE through TRACE, with individual
MOFA_DEBUG_* overrides. Use `MOFA_DIAG(MOFA_SUBSYS_USB, 3, statement)` and the
other project-reserved selectors, never generic DISPLAY/USB tokens. Disabled
statements do not evaluate arguments; keep required counters/checks outside gates.
Large formatting buffers and optional first-capture evidence require preprocessor
gates as well as checking the final binary. Serial diagnostics and persistent
STATS rows have different lifetimes and must not be disabled together.

Run focused policy/source contracts for the subsystem changed; build
`giga_r1_m7` with the authorized project-local paths. Diagnostic changes require
checking release/debug markers and actual child exit codes, not only SUCCESS text.
Native GCC needs its sibling runtime DLL directory on child PATH. Test and build
artifacts are under `.pio/gnss-fifo-p3`, with reused policy objects/fixtures in
`tests` and diagnostic comparison archives in `step230`. An archive is identified
by its ELF/BIN hashes; the common build directory can contain another configuration.
Never upload as an implied build step. Review AGENTS.md before any Git mutation.

## Evidence, limitations and documentation maintenance

STEP 240 changes comments/docs only, on top of the existing uncommitted STEP 230A
work. Its comparison snapshot and comment-only evidence live in
`.pio/gnss-fifo-p3/step240`; do not confuse the total Git diff with this step alone.
The log-clock comment's former phrase "strictly monotonic" was corrected:
same-host-millisecond calls can return equal values, and the initial GNSS anchor
replaces the placeholder epoch. The new contract documents this boundary; no
clock behavior is changed. Optional display first captures are now DEBUG-only,
as required by STEP 230A even where historical reports describe older captures.

Remaining evidence gaps include electrical conditioning, stack/heap peaks under
load, sustainable storage throughput, actual hotplug/abort recovery and visible
panel output. Host tests do not close those gaps. Accepted USB risks remain
accepted; other possible defects found while documenting must be reported and
separately authorized before repair. Update interface comments and focused tests
with future behavior changes rather than allowing them to become new promises.
