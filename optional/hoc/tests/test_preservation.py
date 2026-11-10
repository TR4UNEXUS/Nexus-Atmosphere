"""Regression checks for the established USB Loader and source-only HOC profile.

These inspect source identities and ownership/order; they do not emulate Horizon
or establish that the candidate boots on hardware.
"""
from pathlib import Path
import hashlib
import json
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASELINE = json.loads((ROOT / "evidence/baseline-inputs.json").read_text())
ORIGINAL = Path(BASELINE["original_loader"])
LOADER = ROOT / "Atmosphere/stratosphere/loader"
HELPER = ROOT / "Atmosphere/stratosphere/hoc_patch"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def brace_block(text, anchor):
    """Extract a reviewed C++ block whose fixture contains no quoted braces."""
    start = text.index(anchor)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError("Unterminated block: " + anchor)


class PreservationTests(unittest.TestCase):
    def test_original_combined_loader_was_not_modified(self):
        for relative, expected in BASELINE["original_loader_files"].items():
            with self.subTest(path=relative):
                self.assertEqual(digest(ORIGINAL / relative), expected)

    def test_established_usb_code_remains_byte_identical(self):
        # Includes early-boot exclusions, alias resolution, USB code mounts,
        # USB3 instruction patches, binding retry/release and ro ownership.
        names = (
            "ldr_content_management.cpp", "ldr_content_management.hpp",
            "ldr_loader_service.cpp", "ldr_loader_service.hpp",
            "ldr_patcher.cpp", "ldr_patcher.hpp", "ldr_embedded_usb_patches.inc",
            "ldr_usb_launch_selection.hpp", "ldr_ro_manager.cpp", "ldr_ro_manager.hpp",
            "ldr_meta.cpp", "ldr_meta.hpp", "ldr_capabilities.cpp", "ldr_capabilities.hpp",
        )
        for name in names:
            with self.subTest(path=name):
                self.assertEqual(digest(LOADER / "source" / name),
                                 BASELINE["original_loader_files"]["source/" + name])

    def test_usb_binding_still_precedes_handle_publication(self):
        old = (ORIGINAL / "source/ldr_process_creation.cpp").read_text(encoding="utf-8")
        new = (LOADER / "source/ldr_process_creation.cpp").read_text(encoding="utf-8")
        marker = "if (mount.UsesUsbCode())"
        self.assertEqual(brace_block(new, marker), brace_block(old, marker))
        binding = new.index("BindUsbProcessContent(pin_id")
        register = new.index("RoManager::GetInstance().RegisterProcess", binding)
        publish = new.index("*out = info.process_handle", register)
        self.assertLess(binding, register)
        self.assertLess(register, publish)
        self.assertIn("CloseHandle", brace_block(new, marker))

    def test_unpin_releases_the_same_binding(self):
        old = (ORIGINAL / "source/ldr_process_creation.cpp").read_text(encoding="utf-8")
        new = (LOADER / "source/ldr_process_creation.cpp").read_text(encoding="utf-8")
        self.assertEqual(brace_block(new, "Result UnpinProgram("),
                         brace_block(old, "Result UnpinProgram("))

    def test_patch_engine_and_source_default_cust_preserved(self):
        # This is explicitly the source-default baseline. It does not claim to
        # recover the unavailable installed KIP's personal CUST bytes.
        for relative, expected in BASELINE["original_loader_files"].items():
            if relative.startswith("source/oc/"):
                with self.subTest(path=relative):
                    if relative == "source/oc/pcv/pcv.cpp":
                        original = (ORIGINAL / relative).read_text(encoding="utf-8")
                        expected_text = original.replace("void WriteKipLoadToIram()", "Result WriteKipLoadToIram()")
                        expected_text = expected_text.replace(
                            "R_DISCARD(SmcCopyToIram(LoadMagicAddress, &hocMagic, sizeof(hocMagic)));",
                            "return SmcCopyToIram(LoadMagicAddress, &hocMagic, sizeof(hocMagic));")
                        expected_text = expected_text.replace("        WriteKipLoadToIram();", (
                            "        // Success is acknowledged only after Loader commits the image and its\n"
                            "        // final permissions through the separate service Commit command."))
                        self.assertEqual((HELPER / relative).read_text(encoding="utf-8"), expected_text)
                    elif relative == "source/oc/pcv/pcv.hpp":
                        expected_text = (ORIGINAL / relative).read_text(encoding="utf-8").replace(
                            "    void SafetyCheck();", "    void SafetyCheck();\n    Result WriteKipLoadToIram();")
                        self.assertEqual((HELPER / relative).read_text(encoding="utf-8"), expected_text)
                    else:
                        self.assertEqual(digest(HELPER / relative), expected)

    def test_helper_checks_before_any_patcher_mutation(self):
        source = (HELPER / "source/hoc_service.cpp").read_text(encoding="utf-8")
        patch = brace_block(source, "Result Service::Patch(")
        steps = ("Authenticate(pid, process)", "g_state.CanPrepare(",
                 "crypto::GenerateSha256Hash(input_hash", "std::memcmp(input_hash",
                 "g_state.BeginPrepare(request)", "std::memcpy(output.GetPointer()",
                 "ldr::hoc::pcv::Patch(", "crypto::GenerateSha256Hash(output_hash",
                 "g_state.CompletePrepare(output_hash)", "*out = g_state.Reply()")
        indexes = [patch.index(step) for step in steps]
        self.assertEqual(indexes, sorted(indexes))
        commit = brace_block(source, "Result Service::Commit(")
        steps = ("Authenticate(pid, process)", "g_state.CanCommit(",
                 "WriteKipLoadToIram()", "g_state.CompleteCommit()", "*out = g_state.Reply()")
        indexes = [commit.index(step) for step in steps]
        self.assertEqual(indexes, sorted(indexes))
        self.assertNotIn("WriteKipLoadToIram", patch)
        self.assertEqual(brace_block(source, "Result Service::GetStatus("),
                         "Result Service::GetStatus(sf::Out<nexus_hoc::Status> out) {\n"
                         "        *out = g_state.Snapshot();\n        R_SUCCEED();\n    }")

    def test_helper_cannot_create_or_map_target_processes(self):
        config = json.loads((HELPER / "hoc_patch.json").read_text())
        self.assertEqual(int(config["title_id"], 16), 0x4200000000484F43)
        self.assertNotEqual(config["title_id"], json.loads((LOADER / "loader.json").read_text())["title_id"])
        syscalls = next(item["value"] for item in config["kernel_capabilities"] if item["type"] == "syscalls")
        for name in ("svcCreateProcess", "svcMapProcessMemory", "svcUnmapProcessMemory",
                     "svcMapProcessCodeMemory", "svcUnmapProcessCodeMemory", "svcSetProcessMemoryPermission"):
            self.assertNotIn(name, syscalls)
        self.assertEqual(int(syscalls["svcManageNamedPort"], 16), 0x71)
        self.assertIn("svcGetProcessId", syscalls)
        self.assertIn("svcGetInfo", syscalls)

    def test_all_read_only_build_dependencies_preserved(self):
        for path, expected in BASELINE["dependency_files"].items():
            with self.subTest(path=path):
                self.assertEqual(digest(Path(path)), expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)
