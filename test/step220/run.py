"""STEP 220 runner: all mutable PlatformIO state stays in the project."""
from pathlib import Path
import os
import subprocess
import sys
import json

ROOT = Path(__file__).resolve().parents[2]
LOCAL = ROOT / '.pio/gnss-fifo-p3'
env = os.environ.copy()
paths = dict(CORE_DIR=LOCAL/'core', PACKAGES_DIR=LOCAL/'core/packages',
             PLATFORMS_DIR=LOCAL/'core/platforms', CACHE_DIR=LOCAL/'core/.cache',
             WORKSPACE_DIR=LOCAL, BUILD_DIR=LOCAL/'build', LIBDEPS_DIR=LOCAL/'libdeps')
for key, path in paths.items():
    env['PLATFORMIO_' + key] = str(path)
env.update(TEMP=str(LOCAL/'tmp'), TMP=str(LOCAL/'tmp'),
           PYTHONDONTWRITEBYTECODE='1', PLATFORMIO_SETTING_ENABLE_TELEMETRY='No',
           PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL='0')
# Reject reparse points, including ancestors, before allowing writes.
for path in [ROOT, *ROOT.parents, *LOCAL.rglob('*')]:
    if path.exists() and path.lstat().st_file_attributes & 0x400:
        raise RuntimeError('Reparse point: ' + str(path))
for path in [*paths.values(), LOCAL/'tmp']:
    path.resolve().relative_to(ROOT)
    path.mkdir(parents=True, exist_ok=True)
    probe = path / '.step220-write-probe'
    with probe.open('xb') as f:
        f.write(b'1')
    probe.unlink()
pio = [sys.executable, '-m', 'platformio']
config = json.loads(subprocess.check_output([*pio, 'project', 'config', '--json-output'], cwd=ROOT, env=env))
resolved = dict(config[0][1])
for key, path in paths.items():
    assert Path(resolved[key.lower()]).resolve() == path.resolve(), (key, resolved)
print(json.dumps({k: str(v) for k, v in paths.items()}, indent=2), flush=True)
if len(sys.argv) > 1:
    # File redirection prevents inherited console pipes from hiding the child's
    # actual exit. wait() observes this exact PlatformIO process, not log text.
    with (LOCAL/'step220-build.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen([*pio, *sys.argv[1:]], cwd=ROOT, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        result = process.wait()
    (LOCAL/'step220-exit.json').write_text(json.dumps({'pid': process.pid, 'exit_code': result}) + '\n')
    print('PlatformIO PID', process.pid, 'exit_code', result, flush=True)
    print((LOCAL/'step220-build.log').read_text(encoding='utf-8', errors='replace')[-5000:])
    sys.exit(result)
