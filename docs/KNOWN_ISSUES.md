# Known issues and accepted limitations

Scope: baseline e025292 plus STEP 230 application changes. Categories distinguish
source observations, plausible risks, accepted residual risks and unverified
assumptions. No third-party bug is claimed solely from a suspected race. No
open-ended dependency audit or third-party modification was performed.

## USBHostMSD detach lifetime ? accepted residual risk

**Evidence/observation:** Arduino_USBHostMbed5 0.3.1 `USBHostMSD.cpp` owns mutable
`dev`/bulk endpoint pointers, initialized/reset by its driver lifecycle while
application SCSI requests can still be active. Application lock-free callback
flags and generation checks cannot provide complete software lifetime protection
inside the library. The earlier identified device-pointer detach race remains
accepted by the project owner; no new classification is made here.

**Trigger/impact:** removal during connect/enumeration or a multistage command may
invalidate internal state before an application generation check. Potential
results include failed I/O, a blocked operation or invalid pointer access; a
particular crash is not reproduced by host policy tests.

**Mitigation:** one application logger owns all filesystem calls; open streams
finish cleanup before unmount; changed generation rejects confirmation; partial
mounts require cleanup and retries are bounded. Attempted GNSS blocks become
uncertain without replay. These do not repair internal driver lifetime races.

**Decision/future verification:** accepted, not a STEP 230 blocker. A separately
scoped library/controller investigation should trace detach under active commands,
prove object lifetime and synchronize endpoint/device teardown. Targeted hardware
stress with the exact library build is required.

## STM32 host cancellation ? accepted residual risk, incomplete verification

**Confirmed configuration observation:** installed 0.3.1
`src/targets/TARGET_STM/USBHALHost_STM.h` sets `hhcd->Init.dma_enable = 0`.
The inspected USB host uses CPU/IRQ-driven buffer access, unlike the separate
DMA2D display path. This is not a claim about all STM32 configurations.

**Confirmed implementation observation:** `USBEndpoint_STM.cpp::setState` writes
channel-disable bits and uses a fixed `wait_us(50)` cancellation delay, with a
comment describing missing error handling. A delay is not proof of channel halt.

**Unverified/plausible risks:** complete host-channel cancellation, channel-halt
confirmation, stale interrupt handling and transfer-pointer lifetime were not
proved. An IRQ or controller event after cancellation could reference old transfer
state; no demonstrated corruption is claimed solely from these observations.
No-DMA does not make software/IRQ buffer lifetime safe automatically.

**Trigger/impact/mitigation:** unplug/reconfigure/cancel during active requests
may leave stale state or block progress. Application generation gating and
serialized cleanup reduce application exposure but do not confirm controller
quiescence. The project owner accepts this residual uncertainty. Future work
needs register/IRQ traces, halt acknowledgement and a lifetime proof for transfer
buffers before reuse, scoped separately from this maintenance step.

## Display DMA/HAL ? hardware-dependent assumptions and recovery limitation

**Confirmed application behavior:** checked HAL start/poll and 25 ms timeout;
abort must return success and DMA2D START must be clear. Otherwise the owned
source is permanently pinned, future flushes fail safely, and LVGL flush-ready
is still signaled once. `[F_DISPLAY_PINNED]` reports this once outside flush.

**Unverified assumption:** HAL completion/abort plus START accurately establish
source inactivity on this exact STM32H747/cache configuration, and no other
library writer starts DMA2D concurrently. CPU tests verify the policy ordering,
not bus quiescence, cache coherency, DSI framebuffer switching or visible pixels.
The whole-cache maintenance sequence is inherited application behavior.

**Trigger/impact:** timeout, transfer error, peripheral already active or failed
abort may stop display updates until reboot. Scratch allocation failure also
prevents a flush but retains the previous allocation safely. Quarantine is a
memory-safety fallback, not automatic display recovery.

**Decision/verification:** preserve current safeguards; panel/rotation checks at
all orientations, repeated redraw, allocation-failure injection and timeout/abort
traces remain hardware acceptance work. Do not claim a formerly black display
is visibly fixed from successful compilation alone.

## Other supported integration limitations

| Category | Trigger and impact | Mitigation / acceptance / future verification |
| --- | --- | --- |
| Confirmed finite buffering | 320 GNSS records overflow after 64 s at 5 Hz from empty; 10000 segment candidates per boot | Drop-newest and explicit counters; accepted design. Exercise long disconnect/full-disk cases on target. |
| Confirmed write uncertainty | Short write, flush/close error, detach or power loss | Checked I/O, segment retirement, no replay, explicit uncertain count. No physical durability/exactly-once claim; verify FAT media under interruption. |
| Unverified scheduling/memory bounds | Blocking driver/conversion/Serial calls can delay tasks; heap pressure or deep LVGL draws can exhaust memory/stack | Static FIFO/work buffers, bounded diagnostics and display quarantine. Remeasure all thread watermarks and heap peaks under debug/release load. Old measurements are not current guarantees. |
| Confirmed startup observability limit | Most application thread start return values and touch begin result are not checked | READY explicitly means dispatch attempted; GNSS start and display init failures are reported. Broader startup handling would be separate functional work. |
| Configuration assumption | LVGL 9.6.0 public offset/rotate semantics and partial RGB565 buffers | Tests use actual installed LVGL bodies; unsupported modes/offsets/strides fail closed. Revalidate after library upgrade. |
| Configuration limitation | Disabled LVGL widgets/formats, including ARGB8888, may prevent new EEZ styles from drawing | Current UI works within enabled features; re-enable deliberately for new styles, regenerate EEZ output and test visually. |
| Calendar/clock limitation | UTC conversion supports 2000..2099 and rejects leap seconds; fixed EU CET/CEST rule; RTC/legacy clock sync once | Validity flags and placeholders; no drift correction. Test receiver cold start and long holdover; change timezone policy only explicitly. |
| Evidence limitation | Heap deltas include other threads; trace caller address is not reliable attribution | Preserve bounded frozen evidence, correlate exact ELF and hardware traces. No inferred per-file leak proof. |
| Inherited build warnings | Third-party/generated sources emit compiler warnings (including non-void return warnings) | Record in build log, do not silently patch dependencies; assess separately before release acceptance. |

## Diagnostic acceptance boundary

Project RELEASE removes recurring project serial diagnostics while preserving
error detection, accounting and USB records. Third-party serial behavior is not
rewritten. DEBUG/TRACE can block in the Arduino Serial implementation; fixed
message lengths/rate gates bound output volume, not worst-case wall time. Binary
inspection checks selected project markers, not every possible library message.
No board upload or hardware test is part of STEP 230. Independent review remains
necessary, and accepted USB risks remain explicitly accepted.
