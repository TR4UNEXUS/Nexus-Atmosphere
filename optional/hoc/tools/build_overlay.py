"""Build the HS2 settings overlay, with incompatible package installers omitted."""
from pathlib import Path
import hashlib
import json
import subprocess
import build

ROOT = Path(__file__).resolve().parents[1]
folder = ROOT / 'hoc-clk/overlay'
pins = build.application_sources(ROOT / 'hoc-clk')
command = [str(build.SDK / 'msys2/usr/bin/make.exe'), '-j2', 'LIBNX=' + build.LIBNX.as_posix()]
with (build.QA / 'overlay-build.log').open('w') as log:
    p = subprocess.run(command, cwd=folder, env=build.environment(), stdout=log, stderr=subprocess.STDOUT,
        creationflags=subprocess.CREATE_NO_WINDOW)
assert pins == build.application_sources(ROOT / 'hoc-clk'), 'Source changed during overlay build.'
out = folder / 'out'
record = {'passed': p.returncode == 0, 'command': command, 'source_pins': pins,
    'artifacts': {f.as_posix(): build.sha(f) for f in out.glob('*') if f.is_file()},
    'upstream_package_installer_excluded': True, 'hardware_verified': False}
(build.QA / 'overlay-build.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS' if record['passed'] else 'FAIL', 'HS2 overlay build', p.returncode)
if p.returncode:
    print((build.QA / 'overlay-build.log').read_text()[-6000:])
raise SystemExit(p.returncode)
