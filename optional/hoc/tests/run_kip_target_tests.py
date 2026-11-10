"""Validate production HOC file targeting against local fixtures only."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import struct
import subprocess
import difflib

ROOT = Path(__file__).resolve().parents[1]
QA = ROOT / "evidence"
SHA = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
COMPILER = Path(os.environ.get("NEXUS_HOST_CXX", "g++"))


def preservation():
    provenance = json.loads((QA / "hoc-clk-source-provenance.json").read_text(encoding="utf-8"))
    expected_changes = {"sysmodule/Makefile", "sysmodule/src/file/kip.cpp", "sysmodule/src/file/kip.hpp"}
    unchanged = 0
    for relative, expected in provenance["files"].items():
        if relative in expected_changes:
            continue
        path = ROOT / "hoc-clk" / relative
        assert path.is_file() and SHA(path) == expected, relative
        unchanged += 1
    upstream = list((ROOT / "inputs/upstream-hoc260").rglob("kip.cpp"))
    assert len(upstream) == 1
    original_dir = upstream[0].parent
    current = ROOT / "hoc-clk/sysmodule/src/file"
    changed = (current / "kip.cpp").read_text(encoding="utf-8")
    normalized = changed.replace('kip_target::Open("r+b", sizeof(CustomizeTable))', 'fopen("sdmc:/atmosphere/kips/hoc.kip", "r+b")')
    normalized = normalized.replace('kip_target::Open("rb", sizeof(CustomizeTable))', 'fopen("sdmc:/atmosphere/kips/hoc.kip", "rb")')
    normalized = normalized.replace('crc32::checksum_file(kip_target::Path())', 'crc32::checksum_file("sdmc:/atmosphere/kips/hoc.kip")')
    normalized = normalized.replace('if (!kip_target::IsSeparated()) configValues.values[KipCrc32]', 'configValues.values[KipCrc32]')
    normalized = normalized.replace('if (!kip_target::IsSeparated() && (u64)crc32::', 'if ((u64)crc32::')
    normalized = normalized.replace('        // The split KIP\'s identity/hash was checked above. Its different whole\n'
                                    '        // file CRC is not an upgrade and must never trigger an automatic save.\n', '')
    normalized = normalized.replace("        // Split helper adoption must not replace the old combined KIP's CRC.\n", '')
    original = (original_dir / "kip.cpp").read_text(encoding="utf-8")
    # Current routing removes only these explanatory trailing comments.
    original = original.replace('  // write checksum', '')
    assert normalized == original, "Unexpected kip.cpp change outside path routing/CRC gate:\n" + "\n".join(difflib.unified_diff(original.splitlines(), normalized.splitlines()))
    header = (current / "kip.hpp").read_text(encoding="utf-8")
    header = header.replace('#include "kip_target.hpp"\n', '')
    header = header.replace('        if (kip_target::UseGuard()) return kip_target::ReadTable(f, out, sizeof(*out));\n', '')
    header = header.replace('        if (kip_target::UseGuard()) return kip_target::WriteTable(f, in, sizeof(*in));\n', '')
    original_header = (original_dir / "kip.hpp").read_text(encoding="utf-8")
    assert header.rstrip("\n") == original_header.rstrip("\n"), "Unexpected kip.hpp change:\n" + "\n".join(difflib.unified_diff(original_header.splitlines(), header.splitlines()))
    makefile = (ROOT / "hoc-clk/sysmodule/Makefile").read_text(encoding="utf-8").replace(' src/pwr src/ipc ../../common', ' src/pwr src/ipc')
    assert makefile == (original_dir.parents[1] / "Makefile").read_text(encoding="utf-8"), "Unexpected Makefile change"
    adapter = (current / "kip_target.cpp").read_text(encoding="utf-8")
    assert 'result == MAKERESULT(Module_Kernel, 121)' in adapter
    assert 'if (!file && IsSeparated()) g_guard.Block();' in adapter
    assert 'serviceDispatchOut(&helper, nexus_hoc::StatusCommand, status)' in adapter
    assert 'sha256CalculateHash(out, data, size)' in adapter
    assert 'sha256ContextUpdate' in adapter and 'sha256ContextGetHash' in adapter
    return {"unchanged_upstream_files": unchanged, "allowed_existing_changes": sorted(expected_changes),
            "exact_inverse_patch_checks": ["kip.cpp", "kip.hpp", "Makefile"],
            "source_provenance_sha256": SHA(QA / "hoc-clk-source-provenance.json")}


def main():
    source_preservation = preservation()
    helper = ROOT / "staging/atmosphere/nexus-hoc-test/hoc.kip"
    helper_sha = SHA(helper)
    data = helper.read_bytes()
    size = 26824
    start = 0x100 + struct.unpack_from("<I", data, 0x28)[0] + struct.unpack_from("<I", data, 0x38)[0]
    end = start + struct.unpack_from("<I", data, 0x48)[0]
    offsets = [i for i in range(start, end - size + 1) if data[i:i+4] == b"CUST"]
    assert len(offsets) == 1
    cust = data[offsets[0]:offsets[0]+size]
    installed = json.loads((QA / "installed-hoc-input.json").read_text(encoding="utf-8"))
    assert hashlib.sha256(cust).hexdigest() == installed["cust_sha256"]
    assert cust == Path(installed["cust_file"]).read_bytes()
    env = os.environ.copy(); env["PATH"] = str(COMPILER.parent) + os.pathsep + env.get("PATH", "")
    command = [COMPILER, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O2", "-Icommon",
               "tests/kip_target_tests.cpp", "-o", "evidence/kip-target-tests.exe"]
    results = []
    for args, log in ((command, "kip-target-compile.log"),
                      ([ROOT / "evidence/kip-target-tests.exe", "staging/atmosphere/nexus-hoc-test/hoc.kip"], "kip-target-tests.log")):
        result = subprocess.run([str(x) for x in args], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", timeout=60)
        (QA / log).write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(result.stdout[-5000:])
        results.append(result.stdout)
    assert SHA(helper) == helper_sha, "Fixture tests must not change staged helper"
    count = int(re.search(r"(\d+) KIP-target", results[-1]).group(1))
    paths = [ROOT / "common/nexus_hoc_protocol.hpp", ROOT / "hoc-clk/sysmodule/Makefile",
             *[ROOT / "hoc-clk/sysmodule/src/file" / name for name in ("kip.cpp", "kip.hpp", "kip_target.cpp", "kip_target.hpp", "kip_target_core.hpp")],
             ROOT / "tests/kip_target_tests.cpp", Path(__file__)]
    receipt = {"passed": True, "utc": datetime.now(timezone.utc).isoformat(), "host_checks": count,
               "source_preservation": source_preservation,
               "staged_helper_sha256_before_after": helper_sha,
               "personal_cust_sha256": hashlib.sha256(cust).hexdigest(), "personal_cust_bytes": len(cust),
               "compile_command": [str(x) for x in command], "compiler_sha256": SHA(COMPILER),
               "source_pins": {p.relative_to(ROOT).as_posix(): SHA(p) for p in paths},
               "scope": ["Production Guard selection/parser/read/write logic with local FILE fixtures and injected I/O faults.",
                         "Actual staged personalized helper KIP structure; exact CUST-only changes on local temporary copies.",
                         "Original live OC code remains byte-identical outside narrowly inverted routing/CRC edits."],
               "limits": ["No drive writes or hardware test.", "Digest callback is deterministic host fixture; native adapter uses libnx SHA-256.",
                          "Guard detects external CUST changes, not all executable changes; compatible helper replacement is allowed.",
                          "fflush and readback are checked; this does not guarantee power-loss durability.",
                          "Partial-write failure blocks further operations; no on-drive rollback is claimed."], "drive_writes": False}
    (QA / "kip-target-validation.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(results[-1].strip())
    print(f"{source_preservation['unchanged_upstream_files']} upstream files unchanged; scoped routing inversions and personal CUST SHA pass.")


if __name__ == "__main__":
    main()
