# STEP 230A ? current validation

This section supersedes the firmware measurements and artifact hashes in the
historical STEP 230 report below. The same local archive paths now contain the
STEP 230A outputs; the old hashes below identify the earlier artifacts only.

All four requested findings are addressed: firstParameters/firstTransfer storage
and captures compile only at DISPLAY >=3; operational validation and DMA/error
ownership remain active; README distinguishes step230 artifacts from reused
`.pio/gnss-fifo-p3/tests` artifacts; architecture describes scoped errno guarantees
rather than claiming every diagnostic print preserves errno; the reproducible
build runner now checks all 16 optional markers directly for LEVEL 0.

Targeted validation: macro-collision regression, seven executable diagnostic
configurations and seven display Python contracts passed. Previous unaffected
GNSS/policy regression evidence is reused. Both firmware processes exited 0.
Both archived BINs are byte-identical to their ELF-derived objcopy outputs.
All 16 optional markers are absent from RELEASE, while essential status and
persistent logging markers remain. `git diff --check` passed.

| Bytes | LEVEL 0 | LEVEL 3 | DEBUG minus RELEASE |
| --- | ---: | ---: | ---: |
| RAM | 154544 | 156288 | 1744 |
| Flash | 489640 | 496248 | 6608 |
| BIN | 491976 | 498632 | 6656 |

RELEASE process PID 33820, DEBUG PID 35820; actual exit codes 0/0.
RELEASE ELF SHA-256: `8056350bc0ae4ef7d9b38b0ce9e1f133302694ef996a4af87eb709d4182a53d7`.
RELEASE BIN SHA-256: `9bc50d8b913046cc84829adaae5ad47228084dfec4f0b7408b300b4f8f4ecfe6`.
DEBUG ELF SHA-256: `53bbe730fb66f3d999dfefa78b1a4da01e2df4a04496598696911b6a93bd8b57`.
DEBUG BIN SHA-256: `61a748c5b6f15005eed9426e7e77bc1c6b8b2531fb7488b8126b15391e4c1485`.

No runtime heap savings or hardware validation are inferred from these static
figures. Accepted USB residual risks and display/hardware verification gaps are
unchanged. Prepared for renewed independent review; no release/merge/upload
approval is implied. No CASE delegation was performed.

---

## Historical STEP 230 report (superseded measurements)

# STEP 230 validation and review handoff

Branch `feature/gnss-fifo-logging`, maintenance baseline `e025292` plus the
uncommitted STEP 230 diff. No official version, merge, upload or release approval
is implied. Implementation and validation are complete for independent CASE
review; no CASE agent was started. Hardware acceptance remains outstanding.

## Implemented scope

- Central `src/diagnostics_config.h`: default RELEASE (0), ERROR (1), INFO (2),
  DEBUG (3), TRACE (4), with GNSS/RPM/DISPLAY/USB/LOGGER/MEMORY/RTOS overrides.
- Project-owned diagnostic call sites use `MOFA_SUBSYS_*` selectors that expand
  directly to numeric levels. Generic library macros cannot affect a forwarded
  token-concatenation step because there is no such step. No alias for the
  erroneous `MOFA_DEBUG_0x1` was introduced.
- Formatting buffers/output are gated; error detection, heap observation,
  first-failure/suppression accounting, FIFO completion and persistent file
  formats remain active. Critical status remains independent of verbose tracing.
- English module/interface comments were added to handwritten source/header
  files; generated `src/ui` was not edited. Architecture, risk register and README
  document ownership, timing, recovery, build commands and release preparation.

Changed implementation files: `main.cpp`, `gnss_service.cpp`, `heap_diag.cpp`,
`display_rotation.cpp`, and the new diagnostic configuration. Other handwritten
C/C++ changes are comments in the display/DMA, GNSS/epoch/FIFO, heap, clock,
RPM filter, startup self-test, mount/IO policy and UI-variable interfaces/modules.
Tests changed: GNSS source comparison/normalization and display setup contract;
new diagnostic matrix, macro-collision test and reproducible build runner live
under `test/diagnostics`. Documentation: README, `architecture.md`,
`KNOWN_ISSUES.md` and this report. The existing lowercase architecture filename
is retained to avoid a case-only Git rename on Windows.

## Root causes resolved

The source-comparison regex stopped at a semicolon inside a C++ string. Its
replacement tokenizes C++ literals/comments and balances nested delimiters,
unwrapping only the authorized GNSS level-1 single println payload while
retaining the entire payload. Other subsystems/levels remain visible to the
historical comparison. Added coverage verifies semicolons, commas, nested calls,
character literals, escaped quotes, raw strings, comments, multiple wrappers,
changed payloads and rejection of extra statements. No acceptance assertions
were removed. A malformed Python regression literal was corrected using a
triple-quoted raw string, preserving the intended C++ characters.

The firmware-only macro collision came from `DISPLAY=0x1` expanding before
concatenation. Numeric project-prefixed selectors resolve the collision for all
seven subsystem names. The regression deliberately defines every generic name
and proves filtering and absence of disabled side effects in a static_assert,
then executes the same expression successfully.

The silent native compiler failure was a runtime environment problem, not test
source failure: g++ --version returned 0, but its cc1plus frontend returned
3221225781 (`0xC0000135`, DLL not found), with empty stdout/stderr. The unchanged
compile returned 1. Prepending `G:/msys64/ucrt64/bin` to the child PATH made that
same compile and executable return 0. The runner now supplies this PATH locally;
no global environment/package change was needed. Full commands, cwd, separate
stdout/stderr and return codes are in the local
`step230/compiler-diagnosis/results.json` evidence.

## Test results

| Check | Result / evidence |
| --- | --- |
| Python syntax | `python -m py_compile test/gnss_logger/test_integration.py`: passed; cache redirected into the project |
| Parser regressions and GNSS acceptance | Four targeted tests passed before broader validation |
| Python contracts after selector correction | 11 GNSS/parser + 7 display = 18 passed, 0 failed |
| Macro-collision regression | Compiled with native GCC 16.2.0, -O2 -Wall -Wextra -Werror; compile and runtime exit 0 |
| Diagnostic configurations | Seven executable configurations passed: default, levels 1/2/3/4, targeted USB=4, and global 3 with USB=0/GNSS=4 |
| Unchanged functional policies | Reused nine passing C++ constexpr compile suites and 15,460 passing native application fault assertions; these policies/tests were not altered by the selector/PATH correction |
| Firmware | Both builds completed with actual process exit 0; see below |
| ELF/BIN | objcopy -O binary output byte-identical to each archived firmware.bin |
| Release strings | Five detailed subsystem markers and eleven optional operational/error markers absent; seven essential status markers and two persistent log markers retained |

The former failed attempts are superseded by these results, not presented as
successful evidence. Earlier functional-policy results remain in the local
`.pio/gnss-fifo-p3/step230-tests.log`; affected diagnostic/Python tests were rerun
on the prefixed selector implementation. Reproduce with the README commands.

## Firmware builds and memory

The runner invokes the existing interpreter with `-m platformio run -e giga_r1_m7`.
For each build it sets `PLATFORMIO_BUILD_SRC_FLAGS=-DMOFA_DEBUG_LEVEL=<level>`
and verifies the effective value. Existing `platformio.ini` build flags remain
present; neither platformio.local.ini nor global packages are modified. All
mutable core/packages/platforms/cache/workspace/build/libdeps paths resolve
under `.pio/gnss-fifo-p3`, verified by the established path resolver/probes.

| Measurement | LEVEL 0 RELEASE | LEVEL 3 DEBUG | DEBUG minus RELEASE |
| --- | ---: | ---: | ---: |
| Actual process exit | 0 | 0 | ? |
| Process PID | 9328 | 16332 | ? |
| RAM, bytes | 154712 | 156288 | +1576 |
| Flash, bytes | 490288 | 496248 | +5960 |
| BIN, bytes | 492624 | 498632 | +6008 |
| ELF file, bytes | 1140564 | 1149200 | +8636 |
| ELF-derived BIN matches | yes | yes | ? |

RAM is `.data + .bss` against the configured 523624-byte limit. Flash is
PlatformIO's `.text + .data + .ARM.exidx` against 786432 bytes. BIN additionally
contains `.ARM.extab` (2336 / 2384 bytes).
The separate `.lwip_sec` reservation is 278528 bytes in both;
`.uninitialized` is 8 bytes. Linker `.heap` reservations are
368544 / 366968 bytes, not measured free heap.
Static savings do not establish runtime heap savings, stack headroom, improved
latency or a sustainable logging rate.

| Artifact | SHA-256 |
| --- | --- |
| RELEASE ELF | `3fdce7ad9a0316e35607479e6135111bb6356ffd31d438afff177c505a0783b8` |
| RELEASE BIN | `9790bcd79d7f27aa8f895babd8ad771c35131f4aaa793917e3144a8aa966b9ae` |
| DEBUG ELF | `627632c285d831e767716fc0f92f32eea7f0d0e618761c47fba596aeb16f24f3` |
| DEBUG BIN | `ec18e39a06788f77b830971fed31baa21cce89c599ba3c503f3432b951e4dcdb` |

Local archive roots are `.pio/gnss-fifo-p3/step230/level0` and `level3`, each
containing `build.log`, `process.json`, `configuration.json`, ELF/BIN,
`from-elf.bin`, `sections.txt` and `validation.json`. `comparison.json` combines
the measurements. The common PlatformIO output directory contains the last,
DEBUG build. The production candidate is the archived **level0/firmware.bin**.
Artifacts are local/ignored; the hashes and measurements above make this report
usable even when those archives are not shared.

Inherited framework/library/generated warnings remain in build logs. RELEASE
also warns that the captured heap `failure` variable is unused when detail output
is compiled out; its capture/drain ordering is retained for instrumentation and
source-contract consistency. It is not a compile error. A failed PlatformIO
upgrade check after the build is nonfatal; actual exits above were captured by
waiting on the exact child process, not inferred from SUCCESS text.

## Serial-output inspection

LEVEL 0 excludes `GNSS DIAG win_ms=`, `[USB IO DIAG]`, `[HEAP DIAG]`,
`[DISPLAY ROT DIAG]` and `[LOGGER DIAG]`; all five remain in LEVEL 3. Eleven
additional LEVEL-0 exclusions cover GNSS CFG-RATE, configuration/rate/stale
messages, USB mounted, segment exhaustion/IO/statistics/block errors and the
USB/memory recoverable summary IDs. Results are in `release-extra-strings.json`.

LEVEL 0 retains only project BOOT/READY and critical initialization/unrecoverable
status: F_DISPLAY_INIT, F_DISPLAY_CALLBACK, F_DISPLAY_PINNED, GNSS thread-start
failure and FIFO completion invariant failure. Quarantine reports once outside
flush. READY means dispatch was attempted, not that all peripherals are ready.

`# GNSS_LOG_V1` and `# STATS accepted=` remain in both binaries because they are
persistent USB data, not removable diagnostic strings. Only the serial STATS copy
is gated. Error/suppression counters remain independent of print level. Binary
marker checks are not a claim that every third-party string/output was audited.
DEBUG/TRACE output is bounded in volume but Serial may still block.

## Remaining risks and review boundary

The project owner's acceptance of USBHostMSD detach lifetime and STM32 host
cancellation uncertainty is unchanged. No third-party library was modified or
open-ended audit performed. Application generation checks do not prove internal
pointer lifetime or channel-halt/IRQ quiescence. Display HAL completion/abort,
cache behavior, DMA2D ownership and visible rotated panel output still need target
verification. Quarantine requires reboot; host fakes do not prove bus inactivity.

Hardware acceptance must cover hotplug during I/O, repeated display redraw and
rotation, failure/abort recovery, full media, long outages, sustainable GNSS FIFO
drain and all thread stack/heap peaks. Checked flush/close is not a physical
power-fail durability guarantee. See [known issues](KNOWN_ISSUES.md) for trigger,
impact, mitigation, acceptance and future verification by category.

No upload, commit, push, merge, rebase, reset or CASE delegation was performed.
P2_P3.bash, local configuration and generated UI remain untouched. The next
phase is separately authorized independent CASE review, not automatic promotion
to a release.
