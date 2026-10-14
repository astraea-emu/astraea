#!/usr/bin/env python3
"""Pinned source-built PS5 ELF guest-data store then real typed UD2 (research).

The same public, reviewed converter emits an entire source-owned ELF. Its
verified MOV EAX,42; MOV [RIP+disp32],RAX; UD2 code executes only inside
disposable Unicorn virtual memory. All original PT_LOAD byte ranges, guest
write target, and terminal fault are individually verified. Shared host
pages do NOT provide general sub-page permission enforcement.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys

from execute_pinned_owned_ps5_elf_mediated import load_manifest, map_guest

# Frozen from source-generated artifact on the exact pinned public toolchain.
# Never replace these with a digest computed from the candidate being checked.
EXPECTED_SHA256 = "3a69a4a035a10656c1532b1b8825eb4e361d2f755ac356ddeb7a36932f5c8cd7"
EXPECTED_SIZE = 66600
ENTRY = 0x10
CODE_LENGTH = 14
MARKER_INITIAL = 0x4153545241454101
MARKER_FINAL = 42
PAGE = 4096
STACK = 0x700000
MAX_GUEST_PAGES = 256
REGS = ("RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
        "RIP", "EFLAGS", "FS_BASE", "GS_BASE")


def demand(ok, reason):
    if not ok:
        raise ValueError(reason)


def inspect(title):
    elf = Path(title).read_bytes()
    demand(len(elf) == EXPECTED_SIZE and
           hashlib.sha256(elf).hexdigest() == EXPECTED_SHA256,
           "frozen_source_elf_sha_size_drift")
    demand(elf[:7] == b"\x7fELF\x02\x01\x01" and elf[7] == 9,
           "non_reviewed_ps5_elf_header")
    typ, machine = struct.unpack_from("<HH", elf, 16)
    entry, phoff = struct.unpack_from("<QQ", elf, 24)
    phsize, count = struct.unpack_from("<HH", elf, 54)
    demand((typ, machine, entry, phsize) == (0xFE10, 62, ENTRY, 56)
           and 1 <= count <= 32 and phoff <= len(elf)
           and count * phsize <= len(elf) - phoff,
           "source_linked_ps5_elf_layout_drift")
    regions = []
    pages = {}
    mixed = set()
    for index in range(count):
        kind, flags, off, va, _paddr, fsize, msize, _align = (
            struct.unpack_from("<IIQQQQQQ", elf, phoff + index * 56))
        if kind != 1:
            continue
        demand(flags & ~7 == 0 and msize and fsize <= msize and
               off <= len(elf) and fsize <= len(elf) - off and
               msize <= MAX_GUEST_PAGES * PAGE and
               va <= (1 << 64) - msize,
               "malformed_pt_load")
        end = va + msize
        demand(all(end <= r["va"] or va >= r["end"] for r in regions),
               "overlap_between_source_pt_load_byte_extents")
        regions.append(dict(va=va, end=end, fileoff=off, filesz=fsize,
                            flags=flags))
        for page in range(va // PAGE * PAGE,
                          (end + PAGE - 1) // PAGE * PAGE, PAGE):
            demand(page != STACK and
                   (page in pages or len(pages) < MAX_GUEST_PAGES),
                   "invalid_guest_page_count_or_stack_collision")
            if page in pages:
                if pages[page] != flags:
                    mixed.add(page)
                pages[page] |= flags
            else:
                pages[page] = flags
    demand(len(regions) >= 2 and 0 in pages and 1 <= len(mixed) <= 32,
           "missing_source_mapping_or_mixed_page_evidence")
    text = [r for r in regions if r["flags"] & 1 and
            r["va"] <= ENTRY and ENTRY + CODE_LENGTH <=
            r["va"] + r["filesz"]]
    demand(len(text) == 1, "entry_not_unique_file_backed_executable")
    off = text[0]["fileoff"] + ENTRY - text[0]["va"]
    code = elf[off:off + CODE_LENGTH]
    demand(code == bytes.fromhex("b82a000000488905c4c000000f0b"),
           "source_compiled_rip_store_instruction_changed")
    displacement = struct.unpack_from("<i", code, 8)[0]
    marker = ENTRY + 12 + displacement
    demand(marker == 0xc0e0, "compiled_source_marker_target_drift")
    demand(0 <= marker <= (1 << 64) - 8,
           "rip_store_target_overflow")
    target = [r for r in regions if r["flags"] & 2 and
              r["va"] <= marker and marker + 8 <=
              r["va"] + r["filesz"]]
    demand(len(target) == 1 and
           not (text[0]["va"] <= marker < text[0]["end"]),
           "marker_not_uniquely_source_writable")
    value_offset = target[0]["fileoff"] + marker - target[0]["va"]
    demand(elf[value_offset:value_offset + 8] ==
           MARKER_INITIAL.to_bytes(8, "little"),
           "initial_source_owned_marker_changed")
    return elf, regions, pages, mixed, code, marker


def execute_once(elf, regions, pages, relocations, code, marker):
    from unicorn import (
        Uc, UcError, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE,
        UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_PROT_READ,
        UC_PROT_WRITE, UC_ERR_INSN_INVALID,
    )
    from unicorn import x86_const as x

    uc = Uc(UC_ARCH_X86, UC_MODE_64)
    # Shared research helper stages PT_LOADs and RELATIVE patches, then
    # installs union *storage* permissions. We independently authorize the
    # only actual guest write against the original writable PT_LOAD extent.
    relocated = map_guest(uc, elf, regions, pages, relocations,
                          expected_code=code)
    demand(not any(t <= marker < t + 8 or marker <= t < marker + 8
                   for t in relocated),
           "marker_intersects_relocation_write")
    uc.mem_map(STACK, PAGE, UC_PROT_READ | UC_PROT_WRITE)
    start_sp = STACK + 0x800
    registers = {
        name: getattr(x, "UC_X86_REG_" + name) for name in REGS
    }
    for name, register in registers.items():
        uc.reg_write(register, ENTRY if name == "RIP" else
                     start_sp if name == "RSP" else
                     2 if name == "EFLAGS" else 0)
    initial_pages = {page: bytes(uc.mem_read(page, PAGE)) for page in pages}
    demand(bytes(uc.mem_read(marker, 8)) ==
           MARKER_INITIAL.to_bytes(8, "little"),
           "relative_relocation_changed_initial_marker")
    observed = []
    writes = []
    reads = []
    denied = []

    def instruction(_engine, address, size, _data):
        observed.append((address, size))
        index = len(observed) - 1
        if index >= 3:
            denied.append("additional_guest_instruction")
            uc.emu_stop()
            return
        expected = (ENTRY, ENTRY + 5, ENTRY + 12)[index]
        extent = (5, 7, 2)[index]
        owners = [r for r in regions if r["flags"] & 1 and
                  r["va"] <= address and address + extent <= r["end"]]
        # An invalid UD2 has an undefined Unicorn pre-instruction
        # hook-size field. Authenticate exact original bytes instead.
        supported = (address == expected and len(owners) == 1 and
                     (size == extent if index < 2 else
                      bytes(uc.mem_read(address, 2)) == b"\x0f\x0b"))
        if not supported:
            denied.append("invalid_source_instruction_or_extent")
            uc.emu_stop()

    def on_write(_engine, _access, address, width, value, _data):
        writes.append((address, width, value))
        authorized = [r for r in regions if r["flags"] & 2 and
                      r["va"] <= address and address + width <= r["end"]]
        if not (len(authorized) == 1 and address == marker
                and width == 8 and value == MARKER_FINAL):
            denied.append("unapproved_guest_data_store")
            uc.emu_stop()

    def on_read(_engine, _access, address, width, _value, _data):
        reads.append((address, width))
        denied.append("unapproved_guest_data_load")
        uc.emu_stop()

    uc.hook_add(UC_HOOK_CODE, instruction)
    uc.hook_add(UC_HOOK_MEM_READ, on_read)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    caught = None
    try:
        uc.emu_start(ENTRY, 0, timeout=0, count=4)
    except UcError as exc:
        caught = exc.errno
    demand(caught == UC_ERR_INSN_INVALID,
           f"expected_actual_UD2_exception_got_{caught}")
    demand(not denied and not reads and len(writes) == 1 and
           writes[0] == (marker, 8, MARKER_FINAL),
           "unexpected_guest_effect_or_access")
    demand(len(observed) == 3 and
           observed[0] == (ENTRY, 5) and
           observed[1] == (ENTRY + 5, 7) and
           observed[2][0] == ENTRY + 12,
           "unexpected_guest_instruction_path")
    demand(uc.reg_read(registers["RIP"]) == ENTRY + 12 and
           uc.reg_read(registers["RAX"]) == 42 and
           uc.reg_read(registers["RSP"]) == start_sp and
           uc.reg_read(registers["EFLAGS"]) == 2,
           "incorrect_cpu_state_at_terminal_UD2")
    for name in REGS:
        if name not in ("RAX", "RIP", "RSP", "EFLAGS"):
            demand(uc.reg_read(registers[name]) == 0,
                   "unexpected_general_or_segment_register_mutation")
    expected_pages = {p: bytearray(b) for p, b in initial_pages.items()}
    offset = marker % PAGE
    demand(offset <= PAGE - 8, "unqualified_cross_page_store")
    expected_pages[marker // PAGE * PAGE][offset:offset + 8] = (
        MARKER_FINAL.to_bytes(8, "little"))
    for page, expected in expected_pages.items():
        demand(bytes(uc.mem_read(page, PAGE)) == expected,
               "unaccounted_guest_memory_mutation")
    demand(bytes(uc.mem_read(STACK, PAGE)) == bytes(PAGE),
           "synthetic_stack_changed")
    for target, content in relocated.items():
        demand(bytes(uc.mem_read(target, 8)) == content,
               "prior_relocation_mutated")
    return (tuple((name, uc.reg_read(reg)) for name, reg in registers.items()),
            hashlib.sha256(bytes(uc.mem_read(marker, 8))).hexdigest(),
            len(writes), len(relocated))


def main():
    from unicorn import __version__
    demand(__version__ == "2.1.4", "unreviewed_external_engine_version")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("dependencies")
    args = parser.parse_args()
    elf, regions, pages, mixed, code, marker = inspect(args.elf)
    relocations = load_manifest(args.dependencies, regions)
    first = execute_once(elf, regions, pages, relocations, code, marker)
    second = execute_once(elf, regions, pages, relocations, code, marker)
    demand(first == second, "nonrepeatable_guest_memory_or_terminal_state")
    demand(first[2] == 1 and first[3] == 8, "wrong_guest_effect_census")
    print("C1 SOURCE-OWNED PS5 ELF MEDIATED GUEST STORE PASS")
    print("source_sha256=" + EXPECTED_SHA256)
    print("original_guest_entry=0x10")
    print("source_guest_write_target=" + hex(marker))
    print("guest_store_width_bytes=8")
    print("committed_guest_store_value=42")
    print("typed_terminal_ud2=invalid_instruction")
    print("relative_relocations=8_readback_verified")
    print("full_mapped_guest_pages=post_execution_compared")
    print("mixed_virtual_source_pages=" + str(len(mixed)))
    print("original_byte_range_write_authority=checked")
    print("fresh_runs=2_identical")
    print("source_owned_instruction_count_before_fault=2")
    print("sony_imports=none")
    print("native_outside_authored_execution=none")
    print("production_guest_admission=unchanged")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, IndexError, OverflowError,
            struct.error) as exc:
        print("C1 OWNED PS5 DATA STORE REFUSED: " + str(exc), file=sys.stderr)
        sys.exit(1)
