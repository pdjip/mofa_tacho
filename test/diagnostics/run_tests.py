"""Native diagnostic matrix; stops at the first unexpected failure."""
from pathlib import Path
import os
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / '.pio/gnss-fifo-p3/step230/tests'
CXX = os.environ.get('GNSS_TEST_CXX', 'G:/msys64/ucrt64/bin/g++.exe')
# GCC's cc1plus and generated executables need the sibling runtime DLLs.
# This changes only this runner and its children, never the system environment.
os.environ['PATH'] = str(Path(CXX).resolve().parent) + os.pathsep + os.environ.get('PATH', '')
OUT.mkdir(parents=True, exist_ok=True)
collision = OUT / 'macro-collision.exe'
subprocess.run([CXX, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                str(ROOT / 'test/diagnostics/test_macro_collision.cpp'), '-o', str(collision)], check=True)
subprocess.run([str(collision)], check=True)
print('PASS prefixed subsystem collision regression', flush=True)
for index, flags in enumerate([
    [], ['-DMOFA_DEBUG_LEVEL=1', '-DEXPECT_LEVEL=1'],
    ['-DMOFA_DEBUG_LEVEL=2', '-DEXPECT_LEVEL=2'],
    ['-DMOFA_DEBUG_LEVEL=3', '-DEXPECT_LEVEL=3'],
    ['-DMOFA_DEBUG_LEVEL=4', '-DEXPECT_LEVEL=4'],
    ['-DMOFA_DEBUG_USB=4', '-DEXPECT_USB=4'],
    ['-DMOFA_DEBUG_LEVEL=3', '-DEXPECT_LEVEL=3', '-DMOFA_DEBUG_USB=0', '-DEXPECT_USB=0', '-DMOFA_DEBUG_GNSS=4', '-DEXPECT_GNSS=4'],
]):
    exe = OUT / ('diagnostics-' + str(index) + '.exe')
    subprocess.run([CXX, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', *flags,
                    str(ROOT / 'test/diagnostics/test_diagnostics.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS 7 executable diagnostic configurations', flush=True)
if '--matrix-only' not in sys.argv[1:]:
    subprocess.run([sys.executable, str(ROOT / 'test/gnss_logger/run_tests.py'), '--native-host'], check=True)
