#!/usr/bin/env python3
"""Source-owned, no-real-game test suite for the C0 local baseline tool."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import first_title_c0_baseline as c0

PROFILE = """Astraea retail closure profile
program_headers=14
load_segments=5
load_memory_bytes=524288
executable_load_segments=1
executable_load_memory_bytes=131072
generic_needed=4
sce_needed_modules=4
sce_import_libraries=4
sce_unknown_dynamic_records=0
dynamic_symbols=26
rel_relocations=0
rela_relocations=40
plt_relocations=0
total_relocations=40
tls_present=1
tls_initialized_bytes=0
tls_total_bytes=0
tls_alignment=8
"""
DEPS = """Astraea dependency manifest v0
execution=none
identity_policy=opaque
generic_needed_records=4
sce_needed_module_records=4
sce_import_library_records=4
dynamic_symbol_records=26
relocation_records=40
relocation_unique_symbol_indices=4
relocation_application=not_attempted
resolution=not_attempted
guest_instructions=0
symbol[0].name_hex=7365637265742d6e616d65
"""
DIAG = """Astraea retail diagnostic
boundary=unsupported_dynamic_dependencies
stage=pre_entry
guest_rip=0x10
detail0=12
detail1=0
"""


class FirstTitleC0Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.artifact = root / "authorized-ps5-eboot.elf"
        self.artifact.write_bytes(b"source-owned-elf-fixture-no-game")
        self.astraea = root / "astraea"
        self.astraea.write_text("source owned stub")
        self.reports = {"profile": PROFILE, "dependencies": DEPS,
                        "diagnose": DIAG}

    def mock_call(self, args):
        command = args[1]
        return self.reports[command]

    def baseline(self, *, raw=False, diagnostic=False):
        with patch.object(c0, "checked_command", side_effect=self.mock_call) as p:
            result = c0.build_baseline(
                self.astraea, self.artifact, "PPSA02929",
                "ps5-marked-raw" if raw else "sce", diagnostic)
            expected_calls = 6 if diagnostic else 4
            self.assertEqual(p.call_count, expected_calls)
        return result

    def test_full_static_baseline_has_no_execution(self):
        r = self.baseline()
        self.assertEqual(r["guest_executed_instructions"], 0)
        self.assertEqual(r["snapshot_status"], "read_only_only")
        self.assertFalse(r["imports_resolved"])
        self.assertEqual(r["metrics"]["dependencies"]["relocation_records"], 40)
        self.assertEqual(r["metrics"]["profile"]["total_relocations"], 40)

    def test_hashes_original_authorized_bytes(self):
        r = self.baseline()
        import hashlib
        self.assertEqual(r["source"]["sha256"],
                         hashlib.sha256(self.artifact.read_bytes()).hexdigest())

    def test_no_local_paths_or_symbol_names_escape(self):
        encoded = json.dumps(self.baseline())
        self.assertNotIn(str(self.artifact), encoded)
        self.assertNotIn(str(self.astraea), encoded)
        self.assertNotIn("secret-name", encoded)
        self.assertNotIn("7365637265742d6e616d65", encoded)

    def test_raw_profile_is_an_explicit_mode(self):
        with patch.object(c0, "checked_command", side_effect=self.mock_call) as p:
            r = c0.build_baseline(self.astraea, self.artifact,
                                  "PPSA02929", "ps5-marked-raw")
            self.assertEqual(r["profile"], "ps5-marked-raw")
            self.assertEqual(p.call_args_list[0].args[0][1:3],
                             ["profile", "--ps5-raw-elf"])

    def test_preentry_diagnostic_only_with_preentry_boundary(self):
        with patch.object(c0, "checked_command", side_effect=self.mock_call):
            r = c0.build_baseline(self.astraea, self.artifact, "PPSA02929",
                                  "sce", True)
            self.assertEqual(r["preentry_diagnostic"]["stage"], "pre_entry")

    def test_refuse_unreviewed_game_execution_diagnostic(self):
        self.reports["diagnose"] = DIAG.replace(
            "stage=pre_entry", "stage=native_fault")
        with patch.object(c0, "checked_command", side_effect=self.mock_call):
            with self.assertRaisesRegex(ValueError, "diagnostic_not_proven_preentry"):
                c0.build_baseline(self.astraea, self.artifact, "PPSA02929",
                                  "sce", True)

    def test_wrong_title_id_rejected(self):
        with self.assertRaisesRegex(ValueError, "title_id"):
            c0.build_baseline(self.astraea, self.artifact, "CUSA12345", "sce")

    def test_missing_or_empty_artifact_rejected(self):
        self.artifact.write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "artifact_size"):
            self.baseline()

    def test_profile_dependency_count_divergence_rejected(self):
        self.reports["dependencies"] = DEPS.replace(
            "relocation_records=40", "relocation_records=41")
        with self.assertRaisesRegex(ValueError, "census_mismatch"):
            self.baseline()

    def test_duplicate_report_field_rejected(self):
        with self.assertRaisesRegex(ValueError, "duplicate_or_invalid"):
            c0.parse_report("profile", PROFILE + "program_headers=14\n")

    def test_wrong_report_header_rejected(self):
        with self.assertRaisesRegex(ValueError, "unexpected_report_header"):
            c0.parse_report("profile", DEPS)

    def test_non_deterministic_analyzer_rejected(self):
        with patch.object(c0, "checked_command", side_effect=[
                PROFILE, PROFILE.replace("program_headers=14",
                                         "program_headers=15")]):
            with self.assertRaisesRegex(ValueError, "nondeterministic_profile"):
                c0.build_baseline(self.astraea, self.artifact,
                                  "PPSA02929", "sce")

    def test_analyzer_claims_executed_guest_rejected(self):
        self.reports["dependencies"] = DEPS.replace(
            "guest_instructions=0", "guest_instructions=1")
        with self.assertRaisesRegex(ValueError, "claims_execution"):
            self.baseline()

    def test_unavailable_dynamic_symbol_count_supported(self):
        self.reports["profile"] = PROFILE.replace(
            "dynamic_symbols=26", "dynamic_symbols=unavailable")
        r = self.baseline()
        self.assertIsNone(r["metrics"]["profile"]["dynamic_symbols"])

    def test_unknown_raw_diagnostic_rejected(self):
        with self.assertRaisesRegex(ValueError, "raw_elf_preentry"):
            c0.build_baseline(self.astraea, self.artifact, "PPSA02929",
                              "ps5-marked-raw", True)

    def test_relocation_arithmetic_inconsistency_rejected(self):
        self.reports["profile"] = PROFILE.replace(
            "plt_relocations=0", "plt_relocations=1")
        with self.assertRaisesRegex(ValueError, "relocation_total_mismatch"):
            self.baseline()

    def test_raw_failure_never_echoed(self):
        class Failed:
            returncode = 2
            stdout = "sensitive-artifact-source-name"
            stderr = "/home/user/games/very-private-name"
        with patch.object(c0.subprocess, "run", return_value=Failed()):
            with self.assertRaisesRegex(ValueError, "analyzer_failed") as error:
                c0.checked_command(["astraea", "profile", "file"])
            self.assertNotIn("sensitive", str(error.exception))
            self.assertNotIn("/home/", str(error.exception))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FirstTitleC0Tests)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    print("C0 LOCAL FIRST-TITLE BASELINE SELFTEST PASS")
    print(f"source_owned_preflight_cases={result.testsRun}")
    print("local_game_bytes_uploaded=no")
    print("raw_symbols_and_paths_emitted=no")
    print("guest_instructions_executed=0")
