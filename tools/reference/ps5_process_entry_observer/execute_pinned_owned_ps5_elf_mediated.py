#!/usr/bin/env python3
"""Run a hash-pinned, source-owned, import-free PS5-format ELF to typed UD2.

External Unicorn 2.1.4 research only: no native guest entry, Sony service
binding, commercial artifact, real PS5 entry ABI or production backend.
The source-built ELF and dependency manifest are produced by the existing
pinned BlackBear observer CI worktree, not committed into Astraea.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys

EXPECTED_SHA = "cdeab7a242aa88a90a01494ff85ac68f44b15e7a2e588498927e3aa6aae5a947"
EXPECTED_LENGTH = 66600
ENTRY = 0x10
CODE = bytes.fromhex("b82a0000000f0b")  # MOV EAX,42; UD2
PAGE = 4096
STACK = 0x700000
MAX_PAGE_COUNT = 256
MAX_MEMORY_BYTES = MAX_PAGE_COUNT * PAGE
MAX_U64 = (1 << 64) - 1
EXPECTED_RELOCATIONS = 8


def require(ok, label):
    if not ok:
        raise ValueError(label)


def load_elf(path):
    elf = Path(path).read_bytes()
    require(len(elf) == EXPECTED_LENGTH and
            hashlib.sha256(elf).hexdigest() == EXPECTED_SHA,
            "source_built_elf_sha_or_length_drift")
    require(elf[:7] == b"\x7fELF\x02\x01\x01" and elf[7] == 9,
            "not_reviewed_ps5_elf64")
    typ, machine = struct.unpack_from("<HH", elf, 16)
    entry, phoff = struct.unpack_from("<QQ", elf, 24)
    entsize, nph = struct.unpack_from("<HH", elf, 54)
    require((typ, machine, entry, entsize) == (0xFE10, 62, ENTRY, 56) and
            0 < nph <= 32 and phoff <= len(elf) and
            nph * 56 <= len(elf) - phoff, "source_built_elf_header_drift")
    regions = []
    mapped_pages = {}
    mixed_pages = set()
    for ix in range(nph):
        kind, flags, offset, va, _physical, fsize, msize, align = (
            struct.unpack_from("<IIQQQQQQ", elf, phoff + 56 * ix))
        if kind != 1:
            continue
        require(flags & ~7 == 0 and msize > 0 and fsize <= msize and
                offset <= len(elf) and fsize <= len(elf) - offset and
                msize <= MAX_MEMORY_BYTES and va <= MAX_U64 - msize,
                "invalid_source_load_extent")
        end = va + msize
        for prior in regions:
            require(end <= prior["va"] or va >= prior["end"],
                    "overlapping_source_virtual_extent")
        record = dict(va=va, end=end, fileoff=offset, filesz=fsize,
                      flags=flags, ph_index=ix)
        regions.append(record)
        for page in range(va // PAGE * PAGE,
                          (end + PAGE - 1) // PAGE * PAGE, PAGE):
            require(page != STACK and len(mapped_pages) < MAX_PAGE_COUNT,
                    "source_guest_page_budget_or_stack_collision")
            if page in mapped_pages:
                if mapped_pages[page] != flags:
                    mixed_pages.add(page)
                # Unicorn permissions are page-granular. The union below
                # is a *mediated storage representation*, not guest byte-
                # range authorization. Enforce the original RX extent and
                # forbid all data accesses in this two-opcode fixture.
                mapped_pages[page] |= flags
            else:
                mapped_pages[page] = flags
    require(regions and 0 in mapped_pages and
            sum(r["va"] <= ENTRY and ENTRY + len(CODE) <= r["end"] and
                bool(r["flags"] & 1) for r in regions) == 1,
            "original_entry_not_uniquely_executable")
    executable = next(r for r in regions
                      if r["va"] <= ENTRY and ENTRY + len(CODE) <= r["end"]
                      and r["flags"] & 1)
    code_offset = executable["fileoff"] + ENTRY - executable["va"]
    require(elf[code_offset:code_offset + len(CODE)] == CODE,
            "original_two_instruction_bytes_drift")
    require(len(mixed_pages) <= 32, "unbounded_mixed_permission_pages")
    return elf, regions, mapped_pages, mixed_pages


def load_manifest(path, regions):
    content = Path(path).read_text(encoding="utf-8")
    require(len(content) <= 256000, "dependency_manifest_too_large")
    fields = {}
    for line in content.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        require(key not in fields, "duplicated_manifest_key")
        fields[key] = value
    for key in ("generic_needed_records", "sce_needed_module_records",
                "sce_import_library_records"):
        require(fields.get(key) == "0", "unverified_runtime_import_present")
    require(fields.get("resolution") == "not_attempted" and
            fields.get("guest_instructions") == "0" and
            fields.get("relocation_records") == str(EXPECTED_RELOCATIONS),
            "unsupported_dependency_manifest_shape")
    symbol_count = int(fields["dynamic_symbol_records"])
    require(0 <= symbol_count <= 8, "unexpected_external_symbol_count")
    for i in range(symbol_count):
        if fields.get(f"symbol[{i}].undefined") == "1":
            require(not fields.get(f"symbol[{i}].name_hex"),
                    "undeclared_external_import")
    relocations = []
    previous_targets = set()
    for i in range(EXPECTED_RELOCATIONS):
        pre = f"relocation[{i}]."
        require(fields.get(pre + "table") == "rela" and
                fields.get(pre + "type") == "8" and
                fields.get(pre + "symbol_index") == "0" and
                fields.get(pre + "has_addend") == "1",
                "unreviewed_relative_relocation_semantics")
        raw_addr = fields.get(pre + "target", "")
        raw_addend = fields.get(pre + "addend", "")
        require(re.fullmatch(r"0x[0-9a-fA-F]{1,16}", raw_addr) is not None and
                re.fullmatch(r"-?[0-9]{1,20}", raw_addend) is not None,
                "malformed_relative_relocation")
        target = int(raw_addr, 16)
        addend = int(raw_addend, 10)
        require(0 <= target <= MAX_U64 - 8 and 0 <= addend <= MAX_U64,
                "unrepresentable_zero_bias_relocation")
        writable = [r for r in regions if r["va"] <= target and
                    target + 8 <= r["end"] and r["flags"] & 2]
        require(len(writable) == 1 and
                all(target + 8 <= old or old + 8 <= target
                    for old in previous_targets),
                "unowned_or_overlapping_relative_target")
        previous_targets.add(target)
        relocations.append((target, addend))
    return relocations


def map_guest(uc, elf, regions, pages, relocations):
    from unicorn import UC_PROT_ALL, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC
    for page in sorted(pages):
        uc.mem_map(page, PAGE, UC_PROT_ALL)
    for record in regions:
        if record["filesz"]:
            begin = record["fileoff"]
            uc.mem_write(record["va"],
                         elf[begin:begin + record["filesz"]])
    expected = {}
    for target, value in relocations:
        payload = value.to_bytes(8, "little")
        uc.mem_write(target, payload)
        expected[target] = payload
        require(bytes(uc.mem_read(target, 8)) == payload,
                "checked_relative_write_readback_failed")
    # Set virtual guest permissions only after reviewed load/patch preparation.
    for page, flags in pages.items():
        protection = ((UC_PROT_READ if flags & 4 else 0) |
                      (UC_PROT_WRITE if flags & 2 else 0) |
                      (UC_PROT_EXEC if flags & 1 else 0))
        uc.mem_protect(page, PAGE, protection)
    require(bytes(uc.mem_read(ENTRY, len(CODE))) == CODE,
            "source_entry_mutated_during_relocation")
    return expected


def execute_once(elf, regions, pages, relocations):
    from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_64,
                         UC_PROT_READ, UC_PROT_WRITE, UC_HOOK_CODE,
                         UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
                         UC_ERR_INSN_INVALID)
    from unicorn import x86_const as xr

    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    expected_relocated = map_guest(uc, elf, regions, pages, relocations)
    uc.mem_map(STACK, PAGE, UC_PROT_READ | UC_PROT_WRITE)
    sp = STACK + 0x800
    uc.reg_write(xr.UC_X86_REG_RIP, ENTRY)
    uc.reg_write(xr.UC_X86_REG_RSP, sp)
    uc.reg_write(xr.UC_X86_REG_RAX, 0)
    uc.reg_write(xr.UC_X86_REG_EFLAGS, 2)
    observed = []
    attempted_writes = []
    attempted_reads = []

    def on_code(_engine, address, size, _data):
        observed.append((address, size))
        # Unicorn's code-hook width for the deliberately INVALID UD2
        # has been observed as 0xF1F1F1F1, not an architectural two-byte
        # decode. Only the *first valid MOV* requires a trusted size.
        # For the terminal fault, authenticate the exact pinned bytes
        # and executable source VA separately; require a real UC_ERR
        # exception after this callback, never treat the hook as success.
        expected_extent = (5 if address == ENTRY else
                           2 if address == ENTRY + 5 else 0)
        source_exec = [r for r in regions
                       if (r["flags"] & 1) and r["va"] <= address and
                       expected_extent > 0 and
                       address + expected_extent <= r["end"]]
        authorized = (address == ENTRY and size == 5 or
                      address == ENTRY + 5 and
                      bytes(uc.mem_read(address, 2)) == b"\x0f\x0b")
        if len(source_exec) != 1 or len(observed) > 2 or not authorized:
            uc.emu_stop()

    def on_write(_engine, _access, address, width, _value, _data):
        attempted_writes.append((address, width))
        uc.emu_stop()

    def on_read(_engine, _access, address, width, _value, _data):
        attempted_reads.append((address, width))
        uc.emu_stop()

    uc.hook_add(UC_HOOK_CODE, on_code)
    uc.hook_add(UC_HOOK_MEM_READ, on_read)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    try:
        # Running UD2 must fault. Never treat a fixed instruction count as
        # evidence of the actual terminal exception.
        # The third permitted *budget slot* gives Unicorn room to attempt
        # UD2 rather than stopping at the second code callback before the
        # exception is delivered. An unexpected third instruction is
        # independently rejected by on_code and the exact path assertion.
        uc.emu_start(ENTRY, 0, timeout=0, count=3)
    except UcError as ex:
        require(ex.errno == UC_ERR_INSN_INVALID,
                "wrong_guest_terminal_fault_kind")
    else:
        # Report precise bounded engine state rather than treating a silent
        # pre-fault stop as a typed terminal exception or altering the goal.
        raise ValueError(
            "guest_UD2_did_not_raise_invalid_instruction"
            f": observed={observed!r}"
            f" reads={attempted_reads!r} writes={attempted_writes!r}"
            f" rip=0x{uc.reg_read(xr.UC_X86_REG_RIP):x}"
            f" rax={uc.reg_read(xr.UC_X86_REG_RAX)}")

    require(len(observed) == 2 and observed[0] == (ENTRY, 5) and
            observed[1][0] == ENTRY + 5,
            "unexpected_source_guest_instruction_path")
    require(not attempted_writes and not attempted_reads and
            uc.reg_read(xr.UC_X86_REG_RAX) == 42 and
            uc.reg_read(xr.UC_X86_REG_RIP) == ENTRY + 5 and
            uc.reg_read(xr.UC_X86_REG_RSP) == sp and
            uc.reg_read(xr.UC_X86_REG_EFLAGS) & 0x2,
            "incorrect_architectural_state_at_UD2")
    require(bytes(uc.mem_read(STACK, PAGE)) == bytes(PAGE),
            "source_owned_stack_modified")
    require(bytes(uc.mem_read(ENTRY, len(CODE))) == CODE,
            "source_owned_text_modified")
    for target, payload in expected_relocated.items():
        require(bytes(uc.mem_read(target, 8)) == payload,
                "reviewed_relative_modified_during_guest_entry")
    return (uc.reg_read(xr.UC_X86_REG_RAX),
            uc.reg_read(xr.UC_X86_REG_RIP), len(expected_relocated),
            bytes(uc.mem_read(STACK, PAGE)))


def main():
    from unicorn import __version__
    require(__version__ == "2.1.4", "unreviewed_external_engine_version")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("title")
    parser.add_argument("dependencies")
    args = parser.parse_args()
    elf, regions, pages, mixed = load_elf(args.title)
    relocations = load_manifest(args.dependencies, regions)
    first = execute_once(elf, regions, pages, relocations)
    second = execute_once(elf, regions, pages, relocations)
    require(first == second, "nonrepeatable_terminal_guest_context")
    require(mixed, "expected_source_mixed_page_collision_missing")
    print("C1 SOURCE-OWNED PS5-FORMAT MEDIATED TERMINAL PASS")
    print("source_sha256=" + EXPECTED_SHA)
    print("source_owned_elf_type=0xfe10")
    print("source_owned_first_retired_mov_eax_42=1")
    print("source_owned_terminal_ud2=typed_invalid_instruction")
    print("ud2_hook_size=not_trusted_exact_source_bytes_checked")
    print("checked_relative_relocations=" + str(first[2]))
    print("source_mixed_permission_pages=" + str(len(mixed)))
    print("mixed_page_policy=virtual_storage_union_exact_exec_no_data_access")
    print("repeated_fresh_runs=2_identical")
    print("unresolved_sony_services=none_declared")
    print("native_guest_execution=none")
    print("external_engine=unicorn_2.1.4_research_only")
    print("commercial_C1_process_abi=not_established")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, IndexError, OverflowError,
            struct.error) as exc:
        print("C1 OWNED PS5-FORMAT MEDIATED REFUSED: " + str(exc),
              file=sys.stderr)
        sys.exit(1)
