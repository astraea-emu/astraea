#!/usr/bin/env python3
"""Local, read-only, reproducible C0 baseline for an authorized PS5 ELF.

Runs only Astraea's existing profile/dependencies analyzers, each twice,
and optionally its explicitly pre-entry Linux diagnostic. The tool never
tries to launch guest instructions, retrieve files, upload bytes, or decode
a game's data itself. Only whitelisted aggregate metrics and a local SHA
are emitted. The executable file and original reports remain on the user's
machine; do not commit or upload proprietary title artifacts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys

MAX_ARTIFACT_BYTES = 8 * 1024**3
MAX_CLI_OUTPUT_CHARS = 32 * 1024**2
COMMAND_TIMEOUT_SECONDS = 90

PROFILE_NUMERIC = (
    "program_headers", "load_segments", "load_memory_bytes",
    "executable_load_segments", "executable_load_memory_bytes",
    "generic_needed", "sce_needed_modules", "sce_import_libraries",
    "sce_unknown_dynamic_records", "rel_relocations", "rela_relocations",
    "plt_relocations", "total_relocations", "tls_present",
    "tls_initialized_bytes", "tls_total_bytes", "tls_alignment",
)
DEPENDENCIES_NUMERIC = (
    "generic_needed_records", "sce_needed_module_records",
    "sce_import_library_records", "dynamic_symbol_records",
    "relocation_records", "relocation_unique_symbol_indices",
)
HEADERS = {
    "profile": "Astraea retail closure profile",
    "dependencies": "Astraea dependency manifest v0",
    "diagnose": "Astraea retail diagnostic",
}
TITLE_ID = re.compile(r"PPSA[0-9]{5}\Z")


def refuse(reason):
    raise ValueError("c0_baseline_refused:" + reason)


def parse_report(kind, raw):
    if type(raw) is not str or not raw or len(raw) > MAX_CLI_OUTPUT_CHARS:
        refuse("report_empty_or_exceeds_budget")
    lines = raw.splitlines()
    if not lines or lines[0] != HEADERS[kind]:
        refuse("unexpected_report_header")
    fields = {}
    for line in lines[1:]:
        if "=" not in line or len(line) > 65536:
            refuse("invalid_report_row")
        key, value = line.split("=", 1)
        if (not re.fullmatch(r"[a-zA-Z_][a-zA-Z0-9_.\[\]]{0,127}", key) or
                key in fields):
            refuse("duplicate_or_invalid_report_key")
        fields[key] = value
    return fields


def number(record, key):
    s = record.get(key)
    if s is None or not re.fullmatch(r"(?:0|[1-9][0-9]{0,19})", s):
        refuse("missing_or_invalid_numeric_" + key)
    value = int(s)
    if value > 2**64 - 1:
        refuse("numeric_overflow_" + key)
    return value


def normalize(profile, dependencies):
    p = {key: number(profile, key) for key in PROFILE_NUMERIC}
    raw_symbols = profile.get("dynamic_symbols")
    if raw_symbols == "unavailable":
        p["dynamic_symbols"] = None
    else:
        p["dynamic_symbols"] = number(profile, "dynamic_symbols")

    d = {key: number(dependencies, key)
         for key in DEPENDENCIES_NUMERIC}
    if (dependencies.get("execution") != "none" or
            dependencies.get("resolution") != "not_attempted" or
            dependencies.get("guest_instructions") != "0" or
            dependencies.get("relocation_application") != "not_attempted"):
        refuse("analyzer_report_claims_execution_or_binding")
    if (p["generic_needed"] != d["generic_needed_records"] or
            p["sce_needed_modules"] != d["sce_needed_module_records"] or
            p["sce_import_libraries"] != d["sce_import_library_records"] or
            p["total_relocations"] != d["relocation_records"] or
            (p["dynamic_symbols"] is not None and
             p["dynamic_symbols"] != d["dynamic_symbol_records"])):
        refuse("profile_dependency_census_mismatch")
    if p["executable_load_segments"] > p["load_segments"]:
        refuse("invalid_executable_segment_count")
    if (p["rel_relocations"] + p["rela_relocations"] +
            p["plt_relocations"] != p["total_relocations"]):
        refuse("relocation_total_mismatch")
    if p["tls_present"] > 1 or p["tls_initialized_bytes"] > p["tls_total_bytes"]:
        refuse("tls_extent_invalid")
    return p, d


def digest_local(path):
    if not path.is_file():
        refuse("artifact_not_regular_file")
    size = path.stat().st_size
    if size <= 0 or size > MAX_ARTIFACT_BYTES:
        refuse("artifact_size_out_of_bounds")
    hash_state = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            hash_state.update(chunk)
    if path.stat().st_size != size:
        refuse("artifact_changed_while_hashing")
    return size, hash_state.hexdigest()


def checked_command(argv):
    try:
        result = subprocess.run(argv, capture_output=True, text=True,
                                encoding="utf-8", errors="replace",
                                timeout=COMMAND_TIMEOUT_SECONDS,
                                check=False)
    except (OSError, subprocess.TimeoutExpired):
        refuse("analyzer_unavailable_or_timed_out")
    if (result.returncode != 0 or result.stderr.strip() or
            not result.stdout or len(result.stdout) > MAX_CLI_OUTPUT_CHARS):
        # Never display raw stdout/stderr: they may contain local paths or
        # untrusted source-dependent names.
        refuse("analyzer_failed_or_exceeded_budget")
    return result.stdout


def stable_report(executable, subcommand, artifact, kind):
    argv = [str(executable), *subcommand, str(artifact)]
    first = checked_command(argv)
    second = checked_command(argv)
    if first != second:
        refuse("nondeterministic_" + kind + "_report")
    return parse_report(kind, first)


def build_baseline(executable, artifact, title_id, artifact_format,
                   include_preentry=False):
    if not TITLE_ID.fullmatch(title_id):
        refuse("title_id_must_be_ps5_ppsa_identifier")
    if artifact_format not in ("sce", "ps5-marked-raw"):
        refuse("unsupported_elf_profile")
    if include_preentry and artifact_format != "sce":
        refuse("raw_elf_preentry_diagnostic_not_supported")
    if include_preentry and (sys.platform != "linux" or
                             platform.machine().lower() not in
                             ("x86_64", "amd64")):
        refuse("preentry_diagnostic_requires_linux_x86_64")
    if not executable.is_file():
        refuse("astraea_binary_not_found")
    size, sha = digest_local(artifact)
    extra = ["--ps5-raw-elf"] if artifact_format == "ps5-marked-raw" else []
    p = stable_report(executable, ["profile", *extra], artifact, "profile")
    d = stable_report(executable, ["dependencies", *extra],
                      artifact, "dependencies")
    profile, dependencies = normalize(p, d)
    diagnostic = None
    if include_preentry:
        result = stable_report(executable, ["diagnose"],
                               artifact, "diagnose")
        if result.get("stage") != "pre_entry":
            refuse("diagnostic_not_proven_preentry")
        boundary = result.get("boundary", "")
        if not re.fullmatch(r"[a-z_]{1,80}", boundary):
            refuse("invalid_preentry_boundary")
        diagnostic = {"stage": "pre_entry", "boundary": boundary}
    # No paths, symbol names, exported addresses, filenames or original
    # game content are included in this summary.
    return {
        "schema": "astraea.first-title-c0-baseline/v1",
        "title_id": title_id,
        "profile": artifact_format,
        "source": {"sha256": sha, "byte_length": size},
        "snapshot_status": "read_only_only",
        "guest_executed_instructions": 0,
        "imports_resolved": False,
        "repeated_analyzer_reports": "identical",
        "metrics": {"profile": profile, "dependencies": dependencies},
        "preentry_diagnostic": diagnostic,
        "compatibility_claim": "none",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--astraea", type=Path, required=True,
                        help="Path to your locally built Astraea binary")
    parser.add_argument("--artifact", type=Path, required=True,
                        help="Path to a locally authorized PS5 ELF; not uploaded")
    parser.add_argument("--title-id", required=True,
                        help="Verified PPSA ID for this EXACT PS5 revision")
    parser.add_argument("--format", choices=("sce", "ps5-marked-raw"),
                        default="sce")
    parser.add_argument("--include-preentry", action="store_true",
                        help="Linux x86-64 only; deliberately pre-entry diagnose")
    args = parser.parse_args()
    try:
        result = build_baseline(args.astraea, args.artifact, args.title_id,
                                args.format, args.include_preentry)
    except ValueError as error:
        # Refusal code only. No local path, stderr or game-derived names.
        print(str(error), file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
