"""Compile actual constexpr policies, then run explicitly static source contracts."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".pio" / "gnss-fifo-p3" / "tests"
COMPILER = Path(os.environ.get("GNSS_TEST_CXX", "C:/Users/nicol/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe"))
SUITES = [
    "gnss_epoch/test_epoch.cpp", "local_time/test_local_time.cpp",
    "log_clock/test_log_clock.cpp", "rpm_filter/test_rpm_filter.cpp",
    "ui_self_test/test_self_test.cpp", "gnss_log_fifo/test_fifo.cpp",
    "gnss_logger/test_logger.cpp",
]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for suite in SUITES:
        source = ROOT / "test" / suite
        command = [str(COMPILER), "-std=gnu++17", "-Wall", "-Wextra", "-Werror", "-c",
                   str(source), "-o", str(OUT / (source.parent.name + ".o"))]
        print(subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
        print("PASS constexpr " + suite, flush=True)
    subprocess.run([sys.executable, str(ROOT / "test/gnss_logger/test_integration.py")], cwd=ROOT, check=True)
    print("PASS all 7 constexpr suites and source-contract checks", flush=True)

if __name__ == "__main__":
    main()
