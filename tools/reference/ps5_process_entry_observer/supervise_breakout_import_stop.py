#!/usr/bin/env python3
"""Research-only original Breakout pre-import stop under a bounded controller.

The pinned SharpEmu Breakout ELF is independently authored, not Sony retail
content. It runs inside an external mediated Unicorn research engine, NOT as
host-native PS5 code. The Linux child uses a LIMITED kernel syscall filter.
Worker output remains self-reported and is NOT hostile-worker attestation.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import resource
import sys

import compare_breakout_unicorn as oracle
from owned_bounded_worker_io import capture_bounded

WORKER = Path(__file__).with_name("stop_breakout_unresolved_import.py")
OUTPUT_LIMIT = 4096
ERROR_LIMIT = 4096
TRACE_LIMIT = 65536
BUDGET = 32

# Freeze exact structural, research-only assertions, not unverifiable claims
# about genuine Sony APIs or a real PS5 process-entry ABI.
EXPECTED = (
    "C1 MEDIATED BREAKOUT TYPED IMPORT STOP PASS",
    "original_sha256=" + oracle.PINNED_SHA256,
    "source_backed_preimport_retirements=13",
    "entry_plus_callee_guest_writes=10",
    "requested_instruction_budget=32",
    "stop_kind=unresolved_external_import_before_execution",
    "stop_guest_rip=0x23c",
    "stop_guest_got=0x68c8",
    "first_import_call_retired=0",
    "guest_cpu_context=unchanged_across_import_stop",
    "guest_stack=full_4096_bytes_checked",
    "sony_provider_binding=unverified",
    "native_outside_authored_execution=not_authorized",
    "kernel_denied_host_operations=open_socket_fork",
    "worker_seccomp_policy=partial_deny_not_full_sandbox",
)


def demand(ok, why):
    if not ok:
        raise ValueError(why)


def preflight(elf_path, trace_path):
    elf = Path(elf_path)
    trace = Path(trace_path)
    demand(elf.is_file() and 0 < elf.stat().st_size <= oracle.MAX_ELF,
           "bounded_original_breakout_elf_required")
    demand(trace.is_file() and 0 < trace.stat().st_size <= TRACE_LIMIT,
           "bounded_source_owned_reference_trace_required")
    demand(hashlib.sha256(elf.read_bytes()).hexdigest() ==
           oracle.PINNED_SHA256, "original_breakout_source_identity_drift")
    ref, steps, _mnemonics = oracle.parse_reference(trace)
    demand(ref["stop_rip"] == 0x23c and
           ref["unresolved_got"] == 0x68c8 and
           steps[12][1] == 0x23c,
           "reviewed_breakout_import_stop_identity_drift")


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_CPU, (3, 3))
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))
    resource.setrlimit(resource.RLIMIT_NOFILE, (32, 32))


def one_run(elf_path, trace_path, *, tamper_got=False):
    command = [
        sys.executable, "-B", str(WORKER), str(elf_path),
        str(trace_path), "--budget", str(BUDGET), "--restricted-worker",
    ]
    if tamper_got:
        command.append("--test-tamper-got")
    result = capture_bounded(
        command, timeout_seconds=12,
        stdout_limit=OUTPUT_LIMIT, stderr_limit=ERROR_LIMIT,
        preexec_fn=limits,
        env={"PYTHONHASHSEED": "0", "PYTHONDONTWRITEBYTECODE": "1"},
    )
    demand(result.returncode == 0,
           "mediated_breakout_worker_nonzero:" + str(result.returncode) +
           ":" + result.stderr.decode("utf-8", errors="replace")[-300:])
    demand(not result.stderr, "unexpected_mediated_breakout_stderr")
    try:
        lines = result.stdout.decode("ascii").splitlines()
    except UnicodeDecodeError as exc:
        raise ValueError("non_ascii_breakout_worker_report") from exc
    demand(tuple(lines) == EXPECTED,
           "unverified_or_forged_breakout_worker_report")
    return result.stdout


def run(elf_path, trace_path, *, tamper_got=False):
    demand(sys.platform == "linux" and platform.machine() == "x86_64",
           "linux_x86_64_only")
    preflight(elf_path, trace_path)
    first = one_run(elf_path, trace_path, tamper_got=tamper_got)
    second = one_run(elf_path, trace_path, tamper_got=tamper_got)
    demand(first == second, "supervised_breakout_fresh_runs_diverged")
    print("C1 SUPERVISED ORIGINAL BREAKOUT PREIMPORT PASS")
    print("source_sha256=" + oracle.PINNED_SHA256)
    print("source_owned_trace_reference=checked")
    print("first_import_call_retired=0")
    print("stop_guest_rip=0x23c")
    print("stop_guest_got=0x68c8")
    print("guest_write_effects=10_source_backed")
    print("bounded_worker_stdout_bytes=4096_max")
    print("bounded_worker_stderr_bytes=4096_max")
    print("limited_kernel_host_denials=verified_in_worker")
    print("fresh_worker_processes=2_byte_identical")
    print("controller_report_contract=exact_source_owned_shape")
    print("hostile_worker_report_authenticity=not_established")
    print("external_unicorn=research_only")
    print("native_breakout_entry=none")
    print("sony_services_executed=0")
    print("commercial_ps5_instructions_executed=0")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_elf")
    parser.add_argument("astraea_trace")
    parser.add_argument("--test-tamper-got", action="store_true")
    args = parser.parse_args()
    try:
        run(args.original_elf, args.astraea_trace,
            tamper_got=args.test_tamper_got)
    except (ValueError, OSError, IndexError, KeyError) as exc:
        print("C1 SUPERVISED BREAKOUT REFUSED: " + str(exc),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
