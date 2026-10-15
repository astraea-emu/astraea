#!/usr/bin/env python3
"""Source-pinned mediated Breakout: typed PRE-instruction unresolved-import stop.

Unlike the reviewed count=13 study, execute with a larger finite budget and
explicitly stop on the first original, source-backed imported CALL. This
does NOT invoke Sony services, resolve a provider, or execute host-native
outside-authored instructions. Unicorn code hooks are pre-instruction
observations, not an untrusted-code sandbox or a post-retirement oracle.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

import compare_breakout_unicorn as oracle
import compare_breakout_unicorn_batch as batch

RETIRED = 13
STOP_RIP = 0x23C
FIRST_GOT = 0x68C8
MIN_BUDGET = RETIRED + 1
MAX_BUDGET = 64


def check(condition, reason):
    if not condition:
        raise ValueError(reason)


def stop_source_identity(uc, ref):
    """Match the original call bytes and exact RIP-relative unbound GOT."""
    original = bytes(uc.mem_read(STOP_RIP, 6))
    check(len(original) == 6 and original[:2] == b"\xff\x15",
          "unknown indirect imported CALL encoding")
    displacement = struct.unpack_from("<i", original, 2)[0]
    check(STOP_RIP + 6 + displacement == FIRST_GOT,
          "first imported CALL no longer targets reviewed GOT")
    check(ref["unresolved_got"] == FIRST_GOT and
          int.from_bytes(uc.mem_read(FIRST_GOT, 8), "little") ==
          ref["unresolved_word"],
          "first unbound import identity/value changed")
    return original


def run(original, ref, steps, mnemonics, budget, tamper_got=False,
        restricted_worker=False):
    from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UcError

    check(MIN_BUDGET <= budget <= MAX_BUDGET,
          "invalid bounded service-stop instruction budget")
    uc, registers = batch.setup(original, ref)
    if tamper_got:
        word = bytearray(uc.mem_read(FIRST_GOT, 8))
        word[0] ^= 1
        uc.mem_write(FIRST_GOT, bytes(word))
    expected_stack, expected_writes = batch.expected_stack(ref, steps)

    observed = []
    writes = []
    stops = []
    refusals = []

    def on_write(_engine, _access, address, size, value, _user):
        if not 0 < size <= 8:
            refusals.append("unsupported guest write size")
            uc.emu_stop()
            return
        payload = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")
        writes.append((address, payload))

    def on_instruction(_engine, address, size, _user):
        # Every unreviewed destination is refused BEFORE retirement.
        index = len(observed)
        if index < RETIRED:
            if address != steps[index][0] or not 0 < size <= 15:
                refusals.append("source-backed instruction order changed")
                uc.emu_stop()
                return
            observed.append((address, size))
            return
        if index != RETIRED or address != STOP_RIP or size != 6:
            refusals.append("unexpected post-prefix guest control flow")
            uc.emu_stop()
            return
        if stops:
            refusals.append("duplicate unresolved-import observation")
            uc.emu_stop()
            return
        try:
            source_bytes = stop_source_identity(uc, ref)
        except (ValueError, UcError) as exc:
            refusals.append(str(exc))
            uc.emu_stop()
            return
        before_call = {
            name: uc.reg_read(reg) for name, reg in registers.items()
        }
        check(before_call["rip"] == STOP_RIP,
              "stop must precede imported CALL execution")
        stops.append((address, size, source_bytes, before_call, len(writes)))
        # No attempt to follow the opaque imported GOT word, nor any Sony
        # result, stack push, provider resolution or guest resume.
        uc.emu_stop()

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    uc.hook_add(UC_HOOK_CODE, on_instruction)
    if restricted_worker:
        # Limit specific real host effects immediately before mediated
        # execution. This does NOT authenticate a malicious worker, guard
        # unlisted syscalls or permit source ELF to run host-natively.
        from owned_landlock_fs_research import qualify_mutation_denials
        from owned_mediated_worker_confinement import (
            install_research_filter, denied_host_probes,
        )
        canary = __import__("os").environ.get("ASTRAEA_C1_FS_CANARY", "")
        landlock_abi = qualify_mutation_denials(canary)
        install_research_filter()
        checked_denials = denied_host_probes()
        check(checked_denials == [
            "open:EPERM", "socket:EPERM", "fork:EPERM",
        ], "kernel host-denial probe changed")
    try:
        uc.emu_start(ref["entry"], 0, timeout=0, count=budget)
    except UcError as exc:
        raise ValueError(f"mediated guest execution fault: {exc}") from exc

    check(not refusals, f"typed source/control refusal: {refusals}")
    check(len(observed) == RETIRED and len(stops) == 1,
          "missing or ambiguous pre-instruction import stop")
    check([address for address, _ in observed] ==
          [steps[i][0] for i in range(RETIRED)],
          "source-backed prefix was not completely visited")
    address, width, call_bytes, before_call, write_count = stops[0]
    check(address == STOP_RIP and width == 6 and
          call_bytes[:2] == b"\xff\x15" and
          write_count == len(expected_writes),
          "call stop did not precede all imported effects")
    check(writes == expected_writes,
          "source-backed committed write observations changed")
    check(bytes(uc.mem_read(ref["stack_base"], oracle.PAGE)) ==
          expected_stack, "entire synthetic stack changed at stop")
    check(int.from_bytes(uc.mem_read(ref["relative_target"], 8),
                         "little") == ref["relative_value"],
          "source RELATIVE relocation changed")
    check(int.from_bytes(uc.mem_read(FIRST_GOT, 8), "little") ==
          ref["unresolved_word"],
          "unbound first GOT slot changed at stop")

    last_context = dict(zip(oracle.FIELDS, steps[RETIRED - 1][4]))
    last_context["rip"] = steps[RETIRED - 1][1]
    check(last_context["rip"] == STOP_RIP,
          "expected imported-service boundary changed")
    mask = batch.effective_flag_mask(mnemonics)
    for name, register in registers.items():
        expected = last_context[name]
        current = uc.reg_read(register)
        captured = before_call[name]
        if name == "rflags":
            check((current & mask) == (expected & mask) and
                  (captured & mask) == (expected & mask),
                  "defined RFLAGS changed at unresolved import stop")
        else:
            check(current == expected and captured == expected,
                  f"guest {name} changed after pre-instruction stop")

    print("C1 MEDIATED BREAKOUT TYPED IMPORT STOP PASS")
    print("original_sha256=" + oracle.PINNED_SHA256)
    print("source_backed_preimport_retirements=13")
    print("entry_plus_callee_guest_writes=10")
    print("requested_instruction_budget=" + str(budget))
    print("stop_kind=unresolved_external_import_before_execution")
    print("stop_guest_rip=0x23c")
    print("stop_guest_got=0x68c8")
    print("first_import_call_retired=0")
    print("guest_cpu_context=unchanged_across_import_stop")
    print("guest_stack=full_4096_bytes_checked")
    print("sony_provider_binding=unverified")
    print("native_outside_authored_execution=not_authorized")
    if restricted_worker:
        print("kernel_denied_host_operations=open_socket_fork")
        print("worker_seccomp_policy=partial_deny_not_full_sandbox")
        print("landlock_abi_min=3")
        print("landlock_denied_host_fs=open_write_unlink_mkdir")
        print("landlock_scope=filesystem_mutation_only_not_hostile_sandbox")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_elf")
    parser.add_argument("astraea_trace")
    parser.add_argument("--budget", type=int, default=32)
    parser.add_argument("--test-tamper-got", action="store_true")
    parser.add_argument("--restricted-worker", action="store_true",
                        help="research-only limited host syscall denial")
    args = parser.parse_args()
    try:
        elf = Path(args.original_elf).read_bytes()
        check(hashlib.sha256(elf).hexdigest() == oracle.PINNED_SHA256,
              "original independent executable SHA mismatch")
        ref, steps, mnemonics = oracle.parse_reference(args.astraea_trace)
        check(ref["stop_rip"] == STOP_RIP and
              steps[RETIRED - 1][1] == STOP_RIP,
              "source-backed stopped import identity drift")
        run(elf, ref, steps, mnemonics, args.budget, args.test_tamper_got,
            args.restricted_worker)
    except (ValueError, OSError, IndexError, KeyError, struct.error) as exc:
        print("C1 MEDIATED BREAKOUT IMPORT STOP REFUSED: " + str(exc),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
