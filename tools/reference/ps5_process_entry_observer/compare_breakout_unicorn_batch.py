#!/usr/bin/env python3
"""Bounded batch execution of original, SHA-pinned Breakout in mediated Unicorn.

Research only: exactly 13 original x86-64 instructions with the same
synthetic context and real RELATIVE relocation as the existing Astraea
reference. No 14th instruction, Sony service call, production dependency,
or outside-authored native guest entry is admitted.
"""
import argparse
import hashlib
import sys
from pathlib import Path

import compare_breakout_unicorn as reference

STEPS = 13
REGISTER_NAMES = reference.GPR + ("rip", "rflags", "fs_base", "gs_base")


def check(condition, reason):
    if not condition:
        raise ValueError(reason)


def setup(original, ref):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
    from unicorn import x86_const as xr
    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    mapped_pages = reference.map_original_loads(uc, original)
    base = ref["stack_base"]
    check(base % reference.PAGE == 0 and base not in mapped_pages and
          base <= ref["initial_rsp"] < base + reference.PAGE,
          "pinned synthetic stack location changed")
    uc.mem_map(base, reference.PAGE, 7)
    check(bytes(uc.mem_read(base, reference.PAGE)) ==
          bytes(reference.PAGE), "synthetic stack was not initially zero")
    uc.mem_write(ref["relative_target"],
                 ref["relative_value"].to_bytes(8, "little"))
    check(int.from_bytes(uc.mem_read(ref["unresolved_got"], 8),
                         "little") == ref["unresolved_word"],
          "source's first unbound import was modified")
    registers = {
        name: getattr(xr, "UC_X86_REG_" +
                      ("EFLAGS" if name == "rflags" else name.upper()))
        for name in REGISTER_NAMES
    }
    for name, register in registers.items():
        initial = (ref["entry"] if name == "rip" else
                   ref["initial_rsp"] if name == "rsp" else
                   ref["initial_rflags"] if name == "rflags" else 0)
        uc.reg_write(register, initial)
    check(uc.reg_read(registers["rip"]) == ref["entry"],
          "synthetic entry RIP differs")
    return uc, registers


def effective_flag_mask(mnemonics):
    af_defined = True
    for index in range(STEPS):
        name = "AND" if index == 0 else mnemonics.get(index - 2, "")
        if name == "AND":
            af_defined = False
        if name == "SUB":
            af_defined = True
    return reference.FULL_FLAGS if af_defined else (
        reference.FULL_FLAGS & ~reference.AF)


def expected_stack(ref, steps):
    expected = bytearray(reference.PAGE)
    changes = []
    for index in range(STEPS):
        _, _, address, bytes_written, _ = steps[index]
        if address is None:
            continue
        check(len(bytes_written) == 8 and
              ref["stack_base"] <= address and
              address + len(bytes_written) <=
              ref["stack_base"] + reference.PAGE,
              "source-backed stack write shape drift")
        offset = address - ref["stack_base"]
        expected[offset:offset + len(bytes_written)] = bytes_written
        changes.append((address, bytes_written))
    check(len(changes) == 10, "full entry-plus-callee stack-write count changed")
    return expected, changes


def run_mode(original, ref, steps, mnemonics, guarded):
    from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UcError
    uc, registers = setup(original, ref)
    expected_memory, expected_writes = expected_stack(ref, steps)
    writes = []
    observed_rips = []
    unexpected = []

    def on_write(_engine, _kind, addr, size, value, _user):
        if not 0 < size <= 8:
            unexpected.append("unqualified guest store width")
            uc.emu_stop()
            return
        writes.append((addr, (value & ((1 << (8 * size)) - 1))
                       .to_bytes(size, "little")))

    def on_code(_engine, addr, _size, _user):
        # This code hook is a diagnostic gate, not a native sandbox.
        index = len(observed_rips)
        if index >= STEPS or addr != steps[index][0] or addr == ref["stop_rip"]:
            unexpected.append(f"unreviewed control transfer: {addr:#x}")
            uc.emu_stop()
            return
        observed_rips.append(addr)

    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    if guarded:
        uc.hook_add(UC_HOOK_CODE, on_code)

    try:
        # One bounded engine invocation; no imported CALL can be the
        # fourteenth retirement because count is fixed at exactly 13.
        uc.emu_start(ref["entry"], 0, timeout=0, count=STEPS)
    except UcError as exc:
        raise ValueError(f"bounded mediated batch faulted: {exc}") from exc
    check(not unexpected, "unexpected mediated memory/control effect")
    if guarded:
        check(observed_rips == [steps[i][0] for i in range(STEPS)],
              "guarded batch visited unexpected instruction sequence")
    check(writes == expected_writes,
          "batched hook observations differ from source-backed writes")
    check(bytes(uc.mem_read(ref["stack_base"], reference.PAGE)) ==
          expected_memory, "final synthetic stack differed from reference")
    check(int.from_bytes(uc.mem_read(ref["relative_target"], 8),
                         "little") == ref["relative_value"],
          "original RELATIVE relocation bytes changed")
    check(int.from_bytes(uc.mem_read(ref["unresolved_got"], 8),
                         "little") == ref["unresolved_word"],
          "unresolved imported GOT word changed")

    final = steps[STEPS - 1][4]
    final_state = dict(zip(reference.FIELDS, final))
    final_state["rip"] = steps[STEPS - 1][1]
    check(final_state["rip"] == ref["stop_rip"] == 0x23c,
          "first imported CALL boundary was moved")
    flags_mask = effective_flag_mask(mnemonics)
    for name in REGISTER_NAMES:
        actual = uc.reg_read(registers[name])
        expected = final_state[name]
        if name == "rflags":
            check((actual & flags_mask) == (expected & flags_mask),
                  "defined final RFLAGS diverged")
        else:
            check(actual == expected,
                  f"batched final register {name} diverged")

    mode = "guarded" if guarded else "unhooked"
    print(f"C1 BOUNDED BREAKOUT BATCH CASE PASS: {mode}")
    print(f"{mode}.source_backed_instructions={STEPS}")
    print(f"{mode}.source_backed_guest_stores={len(writes)}")
    print(f"{mode}.import_call_retired=0")


def run(elf_path, trace_path, budget):
    from unicorn import __version__
    check(__version__ == "2.1.4", "unreviewed external CPU engine")
    check(budget == STEPS, "unreviewed instruction budget")
    original = Path(elf_path).read_bytes()
    check(hashlib.sha256(original).hexdigest() == reference.PINNED_SHA256,
          "original independent Breakout ELF changed")
    ref, steps, mnemonics = reference.parse_reference(trace_path)
    check(ref["stop_rip"] == 0x23c and
          steps[STEPS - 1][1] == ref["stop_rip"],
          "unknown imported-service stop")
    for guarded in (False, True):
        run_mode(original, ref, steps, mnemonics, guarded)
    print("C1 ORIGINAL BREAKOUT BOUNDED MEDIATED BATCH PASS")
    print("source_sha256=" + reference.PINNED_SHA256)
    print("engine=unicorn_2.1.4_external_research_only")
    print("batch_invocations=one_per_case")
    print("total_source_backed_steps=13")
    print("all_source_backed_guest_stores=10")
    print("first_import_call=unretired_at_0x23c")
    print("synthetic_context=unchanged")
    print("production_cpu_backend=not_selected")
    print("sony_services=not_executed")
    print("native_breakout_execution=not_authorized")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_elf")
    parser.add_argument("astraea_trace")
    parser.add_argument("--instruction-budget", type=int, default=STEPS)
    args = parser.parse_args()
    try:
        run(args.original_elf, args.astraea_trace,
            args.instruction_budget)
    except (ValueError, OSError, IndexError, KeyError) as exc:
        print(f"C1 BOUNDED BREAKOUT BATCH REFUSED: {exc}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
