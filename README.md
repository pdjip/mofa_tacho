# Arduino Mofa Tacho

Firmware for an Arduino GIGA R1 Cortex-M7 tachometer with GIGA Display Shield,
touch, u-blox GNSS over I2C, two-pulse/revolution RPM input, OneWire temperature
sensor and FAT USB mass-storage logging. GNSS epochs use a bounded FIFO; legacy
RPM/temperature logs retain their current-value sampling semantics.

Maintenance baseline: `e025292` on `feature/gnss-fifo-logging` (STEP 230).
This identifies a development baseline, not an official version number.

## Developer documentation

- [Developer orientation and safe extension guide](docs/DEVELOPER_GUIDE.md)

- [Architecture and data flow](docs/architecture.md)
- [Known issues and accepted USB risks](docs/KNOWN_ISSUES.md)
- [STEP 230 validation and build comparison](docs/STEP230_VALIDATION.md)
- [Project rules](AGENTS.md) and [agent workflow](docs/agent-team.md)

## Prerequisites and build

Use PlatformIO with the Arduino Mbed `giga_r1_m7` environment. `platformio.ini`
is authoritative for board, flags and dependencies. The validated local packages
include ST STM32 20.0.0, Arduino Mbed 4.6.0, ARM GCC 7.2.1, GigaDisplayTouch 1.1.2,
LVGL 9.6.0, USBHostMbed5 0.3.1, SparkFun u-blox 2.2.29, OneWire 2.3.8 and
DallasTemperature 4.0.6. Requirements use the ranges in platformio.ini; do not
upgrade dependencies as an incidental release task.

Hardware pin assignments and electrical assumptions are in the architecture.
Edit the UI in EEZ Studio (`mofa.eez-project`) and regenerate `src/ui`; do not
hand-edit generated files. Local `platformio.local*.ini` files are private and
are neither modified nor versioned by the provided commands.

Existing project-local PlatformIO state is under `.pio/gnss-fifo-p3`. On this
Windows installation, run from the repository root:

```powershell
& 'C:/Users/nicol/.platformio/penv/Scripts/python.exe' test/step220/run.py run -e giga_r1_m7
```

The runner verifies resolved local core, packages, platforms, cache, workspace,
build and libdeps paths and rejects reparse points. It records actual process
exit status; `[SUCCESS]` log text alone is insufficient. No command here uploads.
For a conventional separately provisioned workstation: `pio run -e giga_r1_m7`.

## Diagnostic configurations

Default is RELEASE, level 0. Use compiler definitions in the process environment;
no config file edit is required. `PLATFORMIO_BUILD_SRC_FLAGS` adds project-source
flags while preserving `platformio.ini` build flags and dependency settings.

```powershell
# Production (also the default with no MOFA definitions)
$env:PLATFORMIO_BUILD_SRC_FLAGS = '-DMOFA_DEBUG_LEVEL=0'
# Normal troubleshooting
$env:PLATFORMIO_BUILD_SRC_FLAGS = '-DMOFA_DEBUG_LEVEL=2'
# Detailed diagnosis
$env:PLATFORMIO_BUILD_SRC_FLAGS = '-DMOFA_DEBUG_LEVEL=3'
# Only USB and logger tracing, other subsystems remain RELEASE
$env:PLATFORMIO_BUILD_SRC_FLAGS = '-DMOFA_DEBUG_LEVEL=0 -DMOFA_DEBUG_USB=4 -DMOFA_DEBUG_LOGGER=4'
# Run the build after selecting ONE assignment above
& 'C:/Users/nicol/.platformio/penv/Scripts/python.exe' test/step220/run.py run -e giga_r1_m7
Remove-Item Env:PLATFORMIO_BUILD_SRC_FLAGS
```

Available overrides: `MOFA_DEBUG_GNSS`, `_RPM`, `_DISPLAY`, `_USB`, `_LOGGER`,
`_MEMORY`, `_RTOS`. Each replaces the global level and accepts 0..4. A global 3
with `-DMOFA_DEBUG_MEMORY=0` suppresses heap printing while retaining its capture.
The former `MOFA_VERBOSE_LOGGER_TRACE` switch is replaced by LOGGER level 4 (or
RTOS level 3 for stack-marker diagnostics). RPM currently has no serial trace.

RELEASE keeps BOOT/READY and critical display/GNSS-task/FIFO invariant status.
No recurring project GNSS, RPM, USB, display or heap traces are enabled. ERROR
adds recoverable failures, INFO state/statistics, DEBUG detailed subsystem
evidence and TRACE targeted operation markers. Fault counters and persistent
USB data are independent of these levels. Fixed buffers/rate gates limit volume;
Serial itself may still block. See the architecture for exact level assignments.

## Tests and reproducible comparison

Native GCC 16.2.0 is installed at `G:/msys64/ucrt64/bin/g++.exe` on the validation
machine. The test runner prepends the selected compiler directory to its child
process PATH so GCC frontends and test executables can find their runtime DLLs.
An absolute g++ path alone does not supply those DLLs to cc1plus. This changes
no global settings. The runner stops at the first unexpected failure:

```powershell
$env:GNSS_TEST_CXX = 'G:/msys64/ucrt64/bin/g++.exe'
$env:PATH = 'G:/msys64/ucrt64/bin;' + $env:PATH
$env:PYTHONDONTWRITEBYTECODE = '1'
& 'C:/Users/nicol/.platformio/penv/Scripts/python.exe' test/diagnostics/run_tests.py
& 'C:/Users/nicol/.platformio/penv/Scripts/python.exe' test/diagnostics/build_firmware.py
git diff --check
```

The first command runs the macro-collision regression, seven executable diagnostic configurations, nine constexpr
C++ suites, native application fault scenarios and both Python source-contract
suites. The second builds LEVEL 0 and LEVEL 3 sequentially, preserves each ELF/BIN
and checks selected diagnostic strings plus byte-for-byte ELF-derived BIN
consistency. Build comparison and diagnostic matrix artifacts stay in `.pio/gnss-fifo-p3/step230`.
Reused constexpr objects, LVGL fixtures and the native fault executable are in
`.pio/gnss-fifo-p3/tests`; earlier evidence logs also reside directly under
`.pio/gnss-fifo-p3`. The common
PlatformIO build directory contains the last (DEBUG) build; use the archived
`step230/level0/firmware.bin` when identifying the production artifact.
Host policy checks and a successful firmware link do not replace target testing.

## Release preparation

1. Confirm branch, baseline, clean review scope and the intended compiler flags.
2. Regenerate EEZ only if UI was deliberately changed; review generated outputs.
3. Run native/contracts and both firmware configurations; inspect exit codes,
   strings, RAM/Flash/BIN and the validation report. Archive exact ELF/BIN hashes.
4. Review known issues, accepted USB risks, build warnings and hardware gaps.
5. Request independent CASE review. Merge, official version assignment and
   hardware upload require separate explicit owner authorization.

This branch is preparation for review; no automatic publish, merge or firmware
upload is included. USB detach/cancellation residual risks remain owner-accepted.
