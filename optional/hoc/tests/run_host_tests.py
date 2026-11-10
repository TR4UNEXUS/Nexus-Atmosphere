"""Compile actual protocol/state/client tests and record bounded evidence.

The client uses deterministic OS, IPC and digest fixtures. It does not implement
Horizon IPC, execute HOC target machine code, or prove SHA-256 correctness.
"""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_preservation import brace_block

QA = ROOT / "evidence"
DEFAULT_COMPILER = Path("g++")
COMPILER = Path(os.environ.get("NEXUS_HOST_CXX", str(DEFAULT_COMPILER)))
SHA = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()


def generate_process_fragment():
    path = ROOT / "Atmosphere/stratosphere/loader/source/ldr_process_creation.cpp"
    source = path.read_text(encoding="utf-8")
    function = brace_block(source, "Result CreateProcess(os::NativeHandle *out,")
    guard = "ON_SCOPE_EXIT { nexus_oc::InvalidatePending(); };"
    assert function.count(guard) == 1
    block = brace_block(function, "if (g_is_pcv || g_is_ptm)")
    assert "const Result commit_result = nexus_oc::Commit();" in block
    assert "R_DISCARD(svc::CloseHandle(info.process_handle));" in block
    steps = (guard, "CreateProcessAndLoadAutoLoadModules(", "if (mount.UsesUsbCode())",
             block, "RoManager::GetInstance().RegisterProcess", "*out = info.process_handle;")
    positions = [function.index(step) for step in steps]
    assert positions == sorted(positions), "Process-publication order changed"
    load = brace_block(source, "Result LoadAutoLoadModule(os::NativeHandle")
    assert load.index("CheckSegmentHash(") < load.index("nexus_oc::Patch(")
    assert load.index("nexus_oc::Patch(") < load.index("/* Set permissions. */")
    assert "nexus_oc::Commit(" not in load, "Commit must follow all NSOs and permissions"
    create_and_load = brace_block(source, "Result CreateProcessAndLoadAutoLoadModules(")
    cleanup = "ON_RESULT_FAILURE { R_DISCARD(svc::CloseHandle(process_handle)); };"
    assert create_and_load.index(cleanup) < create_and_load.index("R_RETURN(LoadAutoLoadModules(")
    fragment = QA / "process_commit_fragment.inc"
    fragment.write_text(guard + "\n" + block + "\n", encoding="utf-8")
    return {"source": str(path), "source_sha256": SHA(path),
            "fragment": str(fragment), "fragment_sha256": SHA(fragment),
            "scope": "Actual commit/publication cleanup block; surrounding process loader checked for ordering only."}


def run(command, log, environment):
    result = subprocess.run([str(x) for x in command], cwd=ROOT, env=environment,
                            text=True, encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    (QA / log).write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        print(result.stdout[-6000:])
        raise RuntimeError(f"Failed ({result.returncode}): {command}")
    return result.stdout


def main():
    QA.mkdir(exist_ok=True)
    fragment = generate_process_fragment()
    env = os.environ.copy()
    env["PATH"] = str(COMPILER.parent) + os.pathsep + env.get("PATH", "")
    cases = (("protocol", []), ("service_state", ["-Icommon"]),
             ("client", ["-Icommon", "-Itests/stubs", "-Wno-missing-field-initializers"]))
    results = []
    targeted_refresh = sys.argv[1:] == ["--refresh-client"]
    if sys.argv[1:] and not targeted_refresh:
        raise ValueError("Supported option: --refresh-client")
    if targeted_refresh:
        previous = json.loads((QA / "host-validation.json").read_text(encoding="utf-8"))
        assert previous["passed"] and previous["compiler_sha256"] == SHA(COMPILER)
        for relative in ("common/nexus_hoc_protocol.hpp", "tests/protocol_tests.cpp", "tests/service_state_tests.cpp",
                         "Atmosphere/stratosphere/hoc_patch/source/hoc_service_core.hpp"):
            expected = previous["source_pins"].get(relative, previous["source_pins"].get(relative.replace("/", "\\")))
            assert SHA(ROOT / relative) == expected, relative
        results = [dict(case, reused_unchanged_sources=True) for case in previous["cases"] if case["name"] != "client"]
        cases = [case for case in cases if case[0] == "client"]
    for name, flags in cases:
        binary = "evidence/" + name.replace("_", "-") + "-tests.exe"
        command = [COMPILER, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O2", *flags,
                   "tests/" + name + "_tests.cpp", "-o", binary]
        run(command, name + "-compile.log", env)
        output = run([ROOT / binary], name + "-tests.log", env)
        assertions = int(re.search(r"(\d+) .*checks passed", output).group(1))
        results.append({"name": name, "passed": True, "assertions": assertions,
                        "compile_command": [str(x) for x in command], "binary_sha256": SHA(ROOT / binary)})
        print(output.strip(), flush=True)
    output = run([sys.executable, "tests/test_preservation.py"], "preservation-tests.log", env)
    tests = int(re.search(r"Ran (\d+) tests", output).group(1))
    print(f"{tests} preservation/integration tests passed", flush=True)
    paths = []
    for folder in (ROOT / "common", ROOT / "Atmosphere/stratosphere/loader/source",
                   ROOT / "Atmosphere/stratosphere/hoc_patch/source", ROOT / "tests"):
        paths.extend(p for p in folder.rglob("*") if p.is_file() and "__pycache__" not in p.parts)
    for module in ("loader", "hoc_patch"):
        paths.extend([ROOT / f"Atmosphere/stratosphere/{module}/{module}.json",
                      ROOT / f"Atmosphere/stratosphere/{module}/system_module.mk"])
    ipc_layout = None
    ipc_path = QA / "ipc-layout-validation.json"
    if ipc_path.exists():
        ipc_layout = json.loads(ipc_path.read_text(encoding="utf-8"))
        assert ipc_layout["passed"]
        for path, expected in ipc_layout["files"].items():
            assert SHA(Path(path)) == expected, path
        ipc_layout = {"passed": True, "receipt": str(ipc_path), "receipt_sha256": SHA(ipc_path)}
    receipt = {"passed": True, "utc": datetime.now(timezone.utc).isoformat(),
               "cases": results, "host_assertions": sum(x["assertions"] for x in results),
               "preservation_integration_tests": tests, "production_commit_fragment": fragment,
               "refresh_scope": "actual client and preservation; unchanged protocol/state results retained" if targeted_refresh else "all host tests",
               "real_sdk_ipc_layout": ipc_layout,
               "compiler": str(COMPILER), "compiler_sha256": SHA(COMPILER),
               "source_pins": {p.relative_to(ROOT).as_posix(): SHA(p) for p in sorted(set(paths))},
               "limits": ["No hardware boot test.", "No original PCV/PTM NSO fixture available; no known-image patch parity claimed.",
                          "Client OS/IPC/digest functions are deterministic fixtures; actual client source and production server state are exercised.",
                          "Real-SDK IPC layout is a compile-time check, not a runtime Horizon IPC test.",
                          "8 MiB Loader BSS is resident system-pool memory; hardware headroom not established.",
                          "Helper discovery is bounded; synchronous patch IPC has no hard deadline."],
               "drive_writes": False}
    (QA / "host-validation.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
