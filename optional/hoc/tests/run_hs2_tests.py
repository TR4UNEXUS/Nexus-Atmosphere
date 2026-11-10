"""Exercise the production KIP guard and the extracted production startup body."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_preservation import brace_block

ROOT = Path(__file__).resolve().parents[1]
QA = ROOT / 'evidence'
CXX = Path('g++')
ENV = dict(os.environ, PATH=str(CXX.parent) + os.pathsep + os.environ['PATH'])
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

def run(command, name):
    p = subprocess.run([str(x) for x in command], cwd=ROOT, env=ENV,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
    (QA / (name + '.log')).write_text(p.stdout)
    assert p.returncode == 0, p.stdout[-5000:]
    return p.stdout

source_path = ROOT / 'hoc-clk/sysmodule/src/main.cpp'
source = source_path.read_text()
body = brace_block(source, 'int main(int argc, char **argv)')
assert 'i2cInitialize(' not in brace_block(source, 'void __appInit(void)')
assert body.index('WaitForQLaunch()') < body.index('IsCommittedSeparatedBoot(')
assert body.index('IsCommittedSeparatedBoot(') < body.index('i2cInitialize(')
for call in ('board::Initialize()', 'config::Initialize()', 'clockManager::Initialize()', 'ipcService::Initialize()'):
    assert body.index('IsCommittedSeparatedBoot(') < body.index(call)
assert '#include "file/kip_target.hpp"' in source
fixture = r'''
#include <cstdlib>
#include <cstdio>
#include <cstddef>
using Result = int;
bool g_i2c_initialized = false;
bool ready = false;
unsigned hw = 0, config_calls = 0, clock_calls = 0, ipc_calls = 0;
unsigned file_init = 0, file_exit = 0, process_init = 0, process_exit = 0, waited = 0, queried = 0;
bool R_FAILED(Result r) { return r != 0; }
void fatalThrow(Result) { std::abort(); }
void diagAbortWithResult(Result) { std::abort(); }
constexpr int Module_Libnx = 1, LibnxError_ShouldNotHappen = 2;
Result MAKERESULT(int,int) { return 1; }
Result i2cInitialize() { ++hw; return 0; }
void svcSleepThread(unsigned long long) {}
namespace kip { struct CustomizeTable { char bytes[26824]; }; }
namespace fileUtils { Result Initialize() { ++file_init; return 0; } void Exit() { ++file_exit; } void LogLine(const char*) {} }
namespace processManagement { void Initialize() { ++process_init; } void WaitForQLaunch() { ++waited; } void Exit() { ++process_exit; } }
namespace kip_target { bool IsCommittedSeparatedBoot(std::size_t n) { ++queried; if (n != 26824 || !waited) std::abort(); return ready; } }
namespace config { void Initialize(){++config_calls;} void Refresh(){++config_calls;} void SetEnabled(bool){++config_calls;} void Exit(){++config_calls;} }
namespace board { void Initialize(){++hw;} void Exit(){++hw;} }
namespace clockManager { void Initialize(){++clock_calls;} void SetRunning(bool){++clock_calls;} bool Running(){return false;} void Tick(){++clock_calls;} void WaitForNextTick(){++clock_calls;} void Exit(){++clock_calls;} }
namespace ipcService { void Initialize(){++ipc_calls;} void SetRunning(bool){++ipc_calls;} void Exit(){++ipc_calls;} }
'''
fixture += body.replace('int main(', 'int Hs2Main(') + '\n'
fixture += r'''
int main() {
    if (Hs2Main(0, nullptr) || hw || config_calls || clock_calls || ipc_calls || g_i2c_initialized
        || file_init != 1 || file_exit != 1 || process_init != 1 || process_exit != 1 || waited != 1 || queried != 1) return 1;
    ready = true;
    if (Hs2Main(0, nullptr) || !hw || !config_calls || !clock_calls || !ipc_calls || !g_i2c_initialized
        || file_init != 2 || file_exit != 2 || process_init != 2 || process_exit != 2 || waited != 2 || queried != 2) return 2;
    std::puts("2 production startup branch checks passed");
}
'''
fixture_path = QA / 'hs2-startup-fixture.cpp'
fixture_path.write_text(fixture)
cases = []
for name, src, args in (
    ('hs2-startup', fixture_path, []),
    ('kip-target', ROOT / 'tests/kip_target_tests.cpp', [ROOT / 'staging/atmosphere/nexus-hoc/hoc.kip']),
):
    exe = QA / (name + '-tests.exe')
    command = [CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-O2', '-Icommon', src, '-o', exe]
    run(command, name + '-compile')
    output = run([exe, *args], name + '-tests')
    print(output.strip())
    cases.append({'name': name, 'output': output.strip(), 'compiler_command': [str(x) for x in command], 'binary_sha256': sha(exe)})
paths = [source_path, fixture_path, ROOT / 'hoc-clk/sysmodule/src/file/kip_target.cpp',
    ROOT / 'hoc-clk/sysmodule/src/file/kip_target.hpp', ROOT / 'hoc-clk/sysmodule/src/file/kip_target_core.hpp',
    ROOT / 'common/nexus_hoc_protocol.hpp', ROOT / 'tests/kip_target_tests.cpp', Path(__file__)]
receipt = {'passed': True, 'cases': cases, 'source_pins': {p.relative_to(ROOT).as_posix(): sha(p) for p in paths},
    'helper_sha256': sha(ROOT / 'staging/atmosphere/nexus-hoc/hoc.kip'),
    'no_legacy_fallback': True, 'limits': ['Host fixtures, not Horizon IPC or hardware clock stability.', 'Startup uses extracted production body with instrumented service stubs.']}
(QA / 'hs2-validation.json').write_text(json.dumps(receipt, indent=2) + '\n')
