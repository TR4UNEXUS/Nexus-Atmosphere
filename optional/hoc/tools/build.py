"""Build isolated modules against one pinned, read-only SD14 dependency tree."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = Path(os.environ.get('NEXUS_HOC_ORIGINAL', ROOT / 'reference/Atmosphere'))
SD14 = Path(os.environ.get('NEXUS_HOC_CORE', ROOT.parents[1]))
SDK = Path(os.environ.get('DEVKITPRO', '/opt/devkitpro'))
LIBNX = Path(os.environ.get('LIBNX', SDK / 'libnx'))
ARCHIVE = SD14 / 'libraries/libstratosphere/lib/nintendo_nx_arm64_armv8a/e1_release/libstratosphere.a'
CACHED_ARCHIVE = ROOT / 'dependencies/lib/libstratosphere.a'
MODULE = ROOT / 'Atmosphere/stratosphere/loader'
QA = ROOT / 'evidence'
BUILD = MODULE / 'build/nintendo_nx_arm64_armv8a/e1_release'
OUT = MODULE / 'out/nintendo_nx_arm64_armv8a/e1_release'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()


def inventory(folder):
    return {p.relative_to(folder).as_posix(): sha(p)
            for p in sorted(folder.rglob('*')) if p.is_file()
            and not {'build', 'out', '__pycache__'}.intersection(p.relative_to(folder).parts)}


def dependencies():
    paths = [ARCHIVE, LIBNX / 'lib/libnx.a']
    for folder in (SD14 / 'libraries/libstratosphere/include',
                   SD14 / 'libraries/libvapours/include', SD14 / 'libraries/config',
                   LIBNX / 'include'):
        paths.extend(p for p in folder.rglob('*') if p.is_file() and '.gch' not in p.as_posix())
    for p in ('libraries/libstratosphere/stratosphere.specs',):
        paths.append(SD14 / p)
    for p in ('devkitA64/bin/aarch64-none-elf-g++.exe',
              'devkitA64/bin/aarch64-none-elf-gcc.exe', 'tools/bin/elf2kip.exe',
              'libnx/switch_rules', 'libnx/switch.specs'):
        paths.append(SDK / p)
    return {p.as_posix(): sha(p) for p in sorted(set(paths))}


def prepare():
    assert not MODULE.exists(), 'Refusing to replace an existing module.'
    QA.mkdir(parents=True, exist_ok=True)
    assert not (QA / 'baseline-inputs.json').exists()
    original = ORIGINAL / 'stratosphere/loader'
    initial = inventory(original)
    deps = dependencies()
    shutil.copytree(original, MODULE)
    assert inventory(MODULE) == initial
    template = MODULE / 'system_module.mk'
    data = template.read_bytes()
    old = b'include $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/../../libraries/config/templates/stratosphere.mk'
    assert data.count(old) == 1
    new = ('include ' + (SD14 / 'libraries/config/templates/stratosphere.mk').as_posix()).encode()
    template.write_bytes(data.replace(old, new))
    license_file = ORIGINAL / 'LICENSE'
    if license_file.exists():
        shutil.copy2(license_file, ROOT / 'LICENSE')
    baseline = {'purpose': 'Source-default combined HOC 2.6.0 Loader compatibility baseline only.',
                'installed_hoc_sha256_historical': 'e88b7165ddc21d143b963455e9b5a2563c19b544046816a13eedb6bc95237697',
                'installed_hoc_available': False, 'personal_settings_preserved': False,
                'original_loader': original.as_posix(), 'original_loader_files': initial,
                'staged_loader_files': inventory(MODULE), 'dependency_files': deps,
                'read_only_dependency_root': SD14.as_posix(),
                'note': 'No HOC library headers or object archives are mixed with SD14; all Loader TUs compile anew.'}
    (QA / 'baseline-inputs.json').write_text(json.dumps(baseline, indent=2) + '\n')
    print('Prepared', MODULE, 'with', len(initial), 'original files and', len(deps), 'dependency pins.', flush=True)


def verify_dependencies():
    baseline = json.loads((QA / 'baseline-inputs.json').read_text())
    for path, digest in baseline['dependency_files'].items():
        assert sha(path) == digest, ('Dependency changed', path)
    assert inventory(ORIGINAL / 'stratosphere/loader') == baseline['original_loader_files']
    return baseline


def cache_dependencies():
    baseline = verify_dependencies()
    CACHED_ARCHIVE.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ARCHIVE, CACHED_ARCHIVE)
    assert sha(CACHED_ARCHIVE) == baseline['dependency_files'][ARCHIVE.as_posix()]
    print('PASS pinned SD14 archive copied into isolated dependency cache.', flush=True)


def application_sources(folder):
    excluded = {'.a', '.o', '.d', '.su', '.elf', '.map', '.nro', '.nsp', '.nso', '.npdm', '.nacp'}
    return {p.relative_to(folder).as_posix(): sha(p) for p in sorted(folder.rglob('*'))
            if p.is_file() and p.suffix not in excluded
            and not {'build', 'out', '__pycache__'}.intersection(p.relative_to(folder).parts)}


def build_application(name):
    assert name in ('status', 'hoc-clk')
    folder = ROOT / ('diagnostics' if name == 'status' else 'hoc-clk')
    working = folder if name == 'status' else folder / 'sysmodule'
    sources, shared = application_sources(folder), inventory(ROOT / 'common')
    command = [str(SDK / 'msys2/usr/bin/make.exe'), '-j1', 'LIBNX=' + LIBNX.as_posix()]
    with (QA / (name + '-build.log')).open('w', encoding='utf-8') as log:
        proc = subprocess.run(command, cwd=working, env=environment(), stdout=log,
                              stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    assert application_sources(folder) == sources, 'Application source changed during compilation.'
    assert inventory(ROOT / 'common') == shared, 'Shared header changed during compilation.'
    artifacts = ([folder / 'nexus-hoc-status.nro'] if name == 'status'
                 else list((working / 'out').glob('*')))
    record = {'passed': proc.returncode == 0, 'returncode': proc.returncode,
              'hardware_verified': False, 'source_files': sources, 'shared_files': shared,
              'command': command, 'artifacts': {p.as_posix(): sha(p) for p in artifacts if p.is_file()}}
    (QA / (name + '-build.json')).write_text(json.dumps(record, indent=2) + '\n')
    print('PASS' if record['passed'] else 'FAIL', name, 'build', proc.returncode, flush=True)
    if proc.returncode:
        print((QA / (name + '-build.log')).read_text()[-10000:])
    raise SystemExit(proc.returncode)


def isolated_makefile(module):
    """Remove every build-rule reference to the original dependency archive."""
    path = module / 'system_module.mk'
    data = path.read_text()
    original = '$(ATMOSPHERE_LIBRARIES_DIR)/libstratosphere/$(ATMOSPHERE_LIBRARY_DIR)/libstratosphere.a'
    if original in data:
        data = data.replace(original, '$(NEXUS_PINNED_ARCHIVE)')
        # This unconditional explicit empty recipe also applies inside the
        # recursive make and blocks the SDK %.a rule for the pinned cache.
        data += '\n# Pinned external library: never run an implicit archive rule.\n$(NEXUS_PINNED_ARCHIVE): ;\n'
        path.write_text(data)
    assert original not in path.read_text()
    assert '$(NEXUS_PINNED_ARCHIVE): ;' in path.read_text()


def environment():
    env = dict(os.environ)
    env['PATH'] = ';'.join(str(SDK / p) for p in ('msys2/usr/bin', 'tools/bin', 'devkitA64/bin')) + ';' + env['PATH']
    env['DEVKITPRO'] = SDK.as_posix()
    env['DEVKITA64'] = (SDK / 'devkitA64').as_posix()
    env['LIBNX'] = LIBNX.as_posix()
    env['ATMOSPHERE_LIBRARIES_DIR'] = (SD14 / 'libraries').as_posix()
    return env


def make_args(shared=False):
    settings = '--param=ggc-min-expand=10 --param=ggc-min-heapsize=32768 -fstack-usage'
    if shared:
        settings += ' -I' + (ROOT / 'common').as_posix()
    return [str(SDK / 'msys2/usr/bin/make.exe'), '-j1', '-f', 'system_module.mk',
            'ATMOSPHERE_CHECKED_LIBSTRATOSPHERE=1', 'ATMOSPHERE_COMPILER_NAME=gcc',
            'ATMOSPHERE_BUILD_NAME=e1_release', 'ATMOSPHERE_BOARD=nx-hac-001',
            'ATMOSPHERE_CPU=arm-cortex-a57', 'ATMOSPHERE_GIT_BRANCH=unknown',
            'ATMOSPHERE_GIT_REVISION=unknown', 'ATMOSPHERE_GIT_HASH=0000000000000000',
            'ATMOSPHERE_BUILD_SETTINGS=' + settings,
            'LIBNX=' + LIBNX.as_posix(), 'V=1']


def build_module(name, incremental=False):
    """Compile a source-frozen split module; called only after its owner's GO."""
    assert name in ('loader', 'hoc_patch')
    baseline = verify_dependencies()
    archive_pin = baseline['dependency_files'][ARCHIVE.as_posix()]
    assert sha(CACHED_ARCHIVE) == archive_pin
    module = ROOT / 'Atmosphere/stratosphere' / name
    assert (module / 'system_module.mk').is_file()
    isolated_makefile(module)
    text = (module / 'system_module.mk').read_text()
    assert (SD14 / 'libraries/config/templates/stratosphere.mk').as_posix() in text
    assert 'ATMOSPHERE_CHECKED_LIBSTRATOSPHERE' in text
    sources = inventory(module)
    shared = inventory(ROOT / 'common')
    build = module / 'build/nintendo_nx_arm64_armv8a/e1_release'
    out = module / 'out/nintendo_nx_arm64_armv8a/e1_release'
    build.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    command = make_args(shared=True)
    command.extend(['NEXUS_PINNED_ARCHIVE=' + CACHED_ARCHIVE.as_posix(),
                    'LIBPATHS=-L' + CACHED_ARCHIVE.parent.as_posix() + ' -L' +
                    (LIBNX / 'lib').as_posix()])
    # Recompile only this isolated module. Do not use make -B: it also forces
    # SDK implicit dependency targets, including archives.
    assert build.resolve().is_relative_to(ROOT.resolve())
    if not incremental:
        for pattern in ('*.o', '*.d', '*.su'):
            for path in build.glob(pattern):
                assert path.is_file() and path.parent.resolve() == build.resolve()
                path.unlink()
    (QA / (name + '-compile-inputs.json')).write_text(json.dumps({
        'source_files': sources, 'shared_files': shared, 'command': command}, indent=2) + '\n')
    with (QA / (name + '-build.log')).open('w', encoding='utf-8') as log:
        proc = subprocess.run(command, cwd=module, env=environment(), stdout=log,
                              stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    verify_dependencies()
    assert sha(CACHED_ARCHIVE) == archive_pin
    assert inventory(module) == sources, 'Module source changed during compilation.'
    assert inventory(ROOT / 'common') == shared, 'Shared source changed during compilation.'
    result = {'passed': proc.returncode == 0, 'returncode': proc.returncode,
              'hardware_verified': False, 'incremental': incremental,
              'source_files': sources, 'shared_files': shared, 'command': command,
              'objects': {p.name: sha(p) for p in sorted(build.glob('*.o'))},
              'artifacts': {p.as_posix(): sha(p) for p in out.glob('*') if p.is_file()}}
    (QA / (name + '-build.json')).write_text(json.dumps(result, indent=2) + '\n')
    print('PASS' if result['passed'] else 'FAIL', name, 'build', proc.returncode, flush=True)
    if proc.returncode:
        print((QA / (name + '-build.log')).read_text()[-10000:])
    raise SystemExit(proc.returncode)


def baseline_build():
    baseline = verify_dependencies()
    assert inventory(MODULE) == baseline['staged_loader_files'], 'Baseline source changed.'
    BUILD.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    command = make_args()
    (QA / 'baseline-command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (QA / 'baseline-build.log').open('w', encoding='utf-8') as log:
        proc = subprocess.run(command, cwd=MODULE, env=environment(), stdout=log,
                              stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    verify_dependencies()
    result = {'passed': proc.returncode == 0, 'returncode': proc.returncode,
              'hardware_verified': False, 'settings': 'Source defaults; not personalized.',
              'source_files': inventory(MODULE), 'command': command,
              'dependency_files': baseline['dependency_files'],
              'objects': {p.name: sha(p) for p in sorted(BUILD.glob('*.o'))},
              'artifacts': {p.as_posix(): sha(p) for p in OUT.glob('*') if p.is_file()}}
    (QA / 'baseline-build.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS' if result['passed'] else 'FAIL', 'isolated baseline build', proc.returncode, flush=True)
    if proc.returncode:
        print((QA / 'baseline-build.log').read_text()[-10000:])
    raise SystemExit(proc.returncode)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare', 'baseline_build', 'build_module', 'cache_dependencies', 'build_status', 'build_hoc_clk'])
    parser.add_argument('--module', choices=['loader', 'hoc_patch'])
    parser.add_argument('--incremental', action='store_true')
    args = parser.parse_args()
    if args.action == 'build_module':
        assert args.module, '--module is required'
        build_module(args.module, incremental=args.incremental)
    elif args.action in ('build_status', 'build_hoc_clk'):
        build_application('status' if args.action == 'build_status' else 'hoc-clk')
    else:
        globals()[args.action]()
