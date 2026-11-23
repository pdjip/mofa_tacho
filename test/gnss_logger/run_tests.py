"""Compile actual constexpr policies, then run explicitly static source contracts."""
from pathlib import Path
import os
import hashlib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / ".pio" / "gnss-fifo-p3" / "tests"
COMPILER = Path(os.environ.get("GNSS_TEST_CXX", str(ROOT / ".pio/gnss-fifo-p3/core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe")))
SUITES = [
    "gnss_epoch/test_epoch.cpp", "local_time/test_local_time.cpp",
    "log_clock/test_log_clock.cpp", "rpm_filter/test_rpm_filter.cpp",
    "ui_self_test/test_self_test.cpp", "gnss_log_fifo/test_fifo.cpp",
    "gnss_logger/test_logger.cpp",
    "display_rotation/test_rotation.cpp",
    "display_rotation/test_mapping.cpp",
]


def generate_mapping_fixture():
    """Compile real installed LVGL bodies with small test-only type/API shims."""
    package = ROOT / ".pio/gnss-fifo-p3/libdeps/giga_r1_m7/lvgl"
    source_path = package / "src/display/lv_display.c"
    source = source_path.read_text(encoding="utf-8")
    names = ["lv_display_get_physical_horizontal_resolution", "lv_display_get_physical_vertical_resolution",
             "lv_display_get_horizontal_resolution", "lv_display_get_vertical_resolution",
             "lv_display_get_original_horizontal_resolution", "lv_display_get_original_vertical_resolution",
             "lv_display_get_offset_x", "lv_display_get_offset_y", "lv_display_get_rotation", "lv_display_rotate_area"]
    functions = []
    hashes = {}
    for name in names:
        match = re.search(r"^[\w *]+\b" + name + r"\([^;{}]*\)\s*\{", source, re.M)
        if not match:
            raise RuntimeError("Missing installed LVGL function " + name)
        start = source.index("{", match.start())
        depth, end = 1, start + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        function = source[match.start():end].strip()
        functions.append("constexpr " + function)
        hashes[name] = hashlib.sha256(function.encode("utf-8")).hexdigest()
    prelude = '''// Generated from installed LVGL; no production/library source is modified.
#include <stdint.h>
#include <stddef.h>
enum lv_display_rotation_t { LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90,
    LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270 };
struct lv_display_t { int32_t hor_res = 480, ver_res = 800;
    int32_t physical_hor_res = 0, physical_ver_res = 0, offset_x = 0, offset_y = 0;
    lv_display_rotation_t rotation = LV_DISPLAY_ROTATION_0; };
struct lv_area_t { int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
constexpr lv_display_t *lv_display_get_default() { return nullptr; }
constexpr int32_t lv_area_get_width(const lv_area_t *a) { return a->x2 - a->x1 + 1; }
constexpr int32_t lv_area_get_height(const lv_area_t *a) { return a->y2 - a->y1 + 1; }
#define LOG_NULL_DISPLAY_DEPRECATED_MESSAGE() do {} while(0)
#define LV_CHECK_ARG(condition, action) do { if(!(condition)) { action; } } while(0)
'''
    (OUT / "lvgl_mapping_fixture.h").write_text(prelude + "\n\n".join(functions) + "\n", encoding="utf-8")
    metadata = {"source": str(source_path), "source_sha256": hashlib.sha256(source_path.read_bytes()).hexdigest(),
                "version": json.loads((package / "library.json").read_text(encoding="utf-8"))["version"],
                "body_sha256": hashes, "adaptation": "constexpr prefix only; types/default/logging/argument-check/area helpers are test shims"}
    (OUT / "lvgl_mapping_fixture.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    generate_mapping_fixture()
    for suite in SUITES:
        source = ROOT / "test" / suite
        command = [str(COMPILER), "-std=gnu++17", "-Wall", "-Wextra", "-Werror", "-I", str(OUT), "-c",
                   str(source), "-o", str(OUT / (source.parent.name + "_" + source.stem + ".o"))]
        print(subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
        print("PASS constexpr " + suite, flush=True)
    if "--native-host" in sys.argv[1:]:
        executable = OUT / "step220_faults.exe"
        subprocess.run([str(COMPILER), "-std=gnu++17", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "test/step220/test_faults.cpp"), "-o", str(executable)],
                       cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
    subprocess.run([sys.executable, str(ROOT / "test/gnss_logger/test_integration.py")], cwd=ROOT, check=True)
    subprocess.run([sys.executable, str(ROOT / "test/display_rotation/test_integration.py")], cwd=ROOT, check=True)
    print("PASS all 9 constexpr suites and source-contract checks", flush=True)

if __name__ == "__main__":
    main()
