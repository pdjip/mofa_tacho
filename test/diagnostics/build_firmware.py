"""Build and inspect STEP 230 levels without changing PlatformIO configuration.

All mutable state uses the established project-local paths. Every subprocess is
checked; failure terminates the matrix before the next build. The final common
build directory contains DEBUG; archived level0 is the production candidate.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import runpy
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / '.pio/gnss-fifo-p3'
OUT = LOCAL / 'step230'
OUT.mkdir(parents=True, exist_ok=True)
# Reuse the existing path resolver/reparse checks and write probes. No build is
# started by this import because it receives no arguments.
saved_args = sys.argv
try:
    sys.argv = [str(ROOT / 'test/step220/run.py')]
    context = runpy.run_path(sys.argv[0])
finally:
    sys.argv = saved_args
BASE_ENV = context['env']
PIO = context['pio']
BINUTILS = LOCAL / 'core/packages/toolchain-gccarmnoneeabi/bin'
BUILD = LOCAL / 'build/giga_r1_m7'
DETAIL_MARKERS = ['GNSS DIAG win_ms=', '[USB IO DIAG]', '[HEAP DIAG]',
                  '[DISPLAY ROT DIAG]', '[LOGGER DIAG]']
RELEASE_ONLY_MARKERS = ['GNSS CFG-RATE:', 'GNSS configuration failed;',
    'GNSS persistent PVT epoch-rate mismatch;', 'GNSS PVT stream stale;',
    'USB mounted; existing files preserved', 'GNSS segment names exhausted;',
    'GNSS segment I/O failed;', 'GNSS statistics I/O failed;',
    'GNSS block I/O failed;', '[USB:E_IO]', '[MEMORY:E_ALLOC]']
STATUS_MARKERS = ['[BOOT]', '[READY]', '[F_DISPLAY_PINNED]', '[F_DISPLAY_INIT]',
                  '[F_DISPLAY_CALLBACK]', 'GNSS thread could not be started',
                  'GNSS FIFO completion invariant failed']
PERSISTENT_MARKERS = ['# GNSS_LOG_V1', '# STATS accepted=']


def write_json(path, value):
    with path.open('w', encoding='utf-8', newline='\n') as handle:
        json.dump(value, handle, indent=2)
        handle.write('\n')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def config(env):
    return dict(json.loads(subprocess.check_output(
        [*PIO, 'project', 'config', '--json-output'], cwd=ROOT, env=env))[1][1])


def build_level(level):
    target = OUT / ('level' + str(level))
    target.mkdir(parents=True, exist_ok=True)
    env = BASE_ENV.copy()
    # build_src_flags adds to, rather than replaces, platformio.ini build_flags.
    # It applies only to project sources; third-party objects are unchanged.
    env['PLATFORMIO_BUILD_SRC_FLAGS'] = '-DMOFA_DEBUG_LEVEL=' + str(level)
    effective = config(env)
    flags = effective.get('build_src_flags', [])
    if isinstance(flags, str):
        flags = [flags]
    if ' '.join(flags) != env['PLATFORMIO_BUILD_SRC_FLAGS']:
        raise RuntimeError('Unexpected effective project source flags: ' + repr(flags))
    write_json(target / 'configuration.json', {
        'level': level, 'build_src_flags': flags,
        'build_flags': effective.get('build_flags'),
        'paths': {k: env['PLATFORMIO_' + k] for k in context['paths']},
    })
    started = time.monotonic()
    command = [*PIO, 'run', '-e', 'giga_r1_m7']
    with (target / 'build.log').open('w', encoding='utf-8', newline='') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        result = process.wait()
    evidence = {'level': level, 'pid': process.pid, 'exit_code': result,
                'elapsed_seconds': time.monotonic() - started, 'command': command}
    write_json(target / 'process.json', evidence)
    print('LEVEL', level, 'PID', process.pid, 'actual exit_code', result, flush=True)
    if result:
        # Strict decoding: do not silently replace invalid log text.
        with (target / 'build.log').open('r', encoding='utf-8') as log:
            print(log.read()[-6000:])
        raise SystemExit(result)
    for name in ['firmware.elf', 'firmware.bin']:
        shutil.copy2(BUILD / name, target / name)
    derived = target / 'from-elf.bin'
    subprocess.run([str(BINUTILS / 'arm-none-eabi-objcopy.exe'), '-O', 'binary',
                    str(target / 'firmware.elf'), str(derived)], check=True, env=env)
    data = (target / 'firmware.bin').read_bytes()
    if data != derived.read_bytes():
        raise RuntimeError('ELF-derived BIN differs at level ' + str(level))
    sections = subprocess.check_output([str(BINUTILS / 'arm-none-eabi-size.exe'), '-A',
                                       str(target / 'firmware.elf')], env=env).decode('utf-8')
    with (target / 'sections.txt').open('w', encoding='utf-8', newline='\n') as handle:
        handle.write(sections)
    sizes = {name: int(size) for name, size in re.findall(r'^([.\w]+)\s+(\d+)\s+\d+', sections, re.M)}
    strings = {marker: marker.encode('ascii') in data for marker in
               DETAIL_MARKERS + RELEASE_ONLY_MARKERS + STATUS_MARKERS + PERSISTENT_MARKERS}
    for marker in DETAIL_MARKERS:
        if strings[marker] != (level >= 3):
            raise RuntimeError('Unexpected diagnostic marker at level ' + str(level) + ': ' + marker)
    if level == 0 and any(strings[m] for m in RELEASE_ONLY_MARKERS):
        raise RuntimeError('Optional diagnostic string remains in RELEASE')
    for marker in STATUS_MARKERS + PERSISTENT_MARKERS:
        if not strings[marker]:
            raise RuntimeError('Missing operational/persistent marker: ' + marker)
    evidence.update(ram_bytes=sizes['.data'] + sizes['.bss'],
                    flash_bytes=sizes['.text'] + sizes['.data'] + sizes['.ARM.exidx'],
                    bin_bytes=len(data), elf_bytes=(target / 'firmware.elf').stat().st_size,
                    elf_sha256=sha(target / 'firmware.elf'),
                    bin_sha256=sha(target / 'firmware.bin'),
                    elf_bin_byte_identical=True, markers=strings, sections=sizes)
    write_json(target / 'validation.json', evidence)
    print(json.dumps(evidence, indent=2), flush=True)
    return evidence


if __name__ == '__main__':
    results = [build_level(level) for level in (0, 3)]
    comparison = {key: results[1][key] - results[0][key]
                  for key in ('ram_bytes', 'flash_bytes', 'bin_bytes')}
    write_json(OUT / 'comparison.json', {'builds': results, 'debug_minus_release': comparison})
    print('PASS both firmware builds, ELF/BIN and diagnostic marker checks', flush=True)
