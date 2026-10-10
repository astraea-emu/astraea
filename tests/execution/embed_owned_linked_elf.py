#!/usr/bin/env python3
"""Verify and embed one full host-linked, source-authored ELF for owned C1 tests.

The linked ELF retains its ordinary ELF header, program headers, symbol and
section tables. The sealed worker performs only a separately validated,
test-only base relocation and synthetic gate-address fixup on *copies*.
No guest code is executed by this build-time checker.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct

EXPECTED_TEXT = bytes.fromhex("48bf2a0000000000000048b80000000000000000ffe0")
LINK_VA = 0x1000
TEXT_OFFSET = 0x1000
MAX_BYTES = 65536


class LinkedElfError(ValueError):
    pass


def bounded(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise LinkedElfError("ELF section outside input")
    return data[offset:offset + size]


def cstring(table: bytes, offset: int) -> bytes:
    if offset >= len(table):
        raise LinkedElfError("ELF name offset out of bounds")
    end = table.find(b"\0", offset)
    if end < 0:
        raise LinkedElfError("unterminated ELF name")
    return table[offset:end]


def check_linked_elf(data: bytes) -> None:
    if not 64 <= len(data) <= MAX_BYTES or data[:7] != b"\x7fELF\x02\x01\x01":
        raise LinkedElfError("expected bounded ELF64 little-endian linked file")
    typ, machine, version = struct.unpack_from("<HHI", data, 16)
    if (typ, machine, version) != (2, 62, 1):
        raise LinkedElfError("expected native x86-64 ET_EXEC source output")
    entry, phoff, shoff = struct.unpack_from("<QQQ", data, 24)
    ehsize, phentsize, phnum, shentsize, shnum, shstr = struct.unpack_from(
        "<HHHHHH", data, 52)
    if (entry, phoff, ehsize, phentsize, phnum) != (LINK_VA, 64, 64, 56, 1):
        raise LinkedElfError("unexpected linked ELF entry/program header layout")
    if shentsize != 64 or not 2 <= shnum <= 64 or shstr >= shnum:
        raise LinkedElfError("unexpected linked ELF section table")
    bounded(data, phoff, phnum * phentsize)
    bounded(data, shoff, shnum * shentsize)

    p_type, flags, off, va, _pa, filesz, memsz, align = struct.unpack_from(
        "<IIQQQQQQ", data, phoff)
    if (p_type, flags, off, va, filesz, memsz, align) != (
        1, 5, TEXT_OFFSET, LINK_VA,
        len(EXPECTED_TEXT), len(EXPECTED_TEXT), 0x1000,
    ):
        raise LinkedElfError("unexpected linked PT_LOAD or executable bytes")
    if bounded(data, off, filesz) != EXPECTED_TEXT:
        raise LinkedElfError("linked source instructions changed")

    sections = [
        struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * 64)
        for i in range(shnum)
    ]
    shstrhdr = sections[shstr]
    if shstrhdr[1] != 3:
        raise LinkedElfError("section-name table is not STRTAB")
    section_names = bounded(data, shstrhdr[4], shstrhdr[5])
    names = [cstring(section_names, section[0]) for section in sections]
    if names.count(b".text") != 1:
        raise LinkedElfError("ambiguous executable source section")
    text_index = names.index(b".text")
    text = sections[text_index]
    if (text[1], text[4], text[5], text[3]) != (
        1, TEXT_OFFSET, len(EXPECTED_TEXT), LINK_VA,
    ):
        raise LinkedElfError("executable section diverged from program header")
    if any(section[1] in (4, 9, 6, 11) for section in sections):
        raise LinkedElfError("dynamic tables or unresolved relocation sections")
    symbols = [section for section in sections if section[1] == 2]
    if len(symbols) != 1:
        raise LinkedElfError("linked source must retain one symbol table")
    sym = symbols[0]
    if sym[9] != 24 or sym[5] % 24 or sym[6] >= shnum:
        raise LinkedElfError("malformed linked symbol table")
    strsec = sections[sym[6]]
    if strsec[1] != 3:
        raise LinkedElfError("symbol string table must be STRTAB")
    strings = bounded(data, strsec[4], strsec[5])
    matches = []
    for i in range(sym[5] // 24):
        off_name, info, other, section, address, size = struct.unpack(
            "<IBBHQQ", bounded(data, sym[4] + i * 24, 24))
        if cstring(strings, off_name) == b"astraea_owned_linked_entry":
            matches.append((info, other, section, address, size))
    if matches != [(0x12, 0, text_index, LINK_VA, len(EXPECTED_TEXT))]:
        raise LinkedElfError("source-owned linked entry identity changed")


def self_test(data: bytes) -> None:
    check_linked_elf(data)
    for offset in (0, 16, 24, 64, TEXT_OFFSET, TEXT_OFFSET + 12):
        changed = bytearray(data)
        changed[offset] ^= 1
        try:
            check_linked_elf(bytes(changed))
        except LinkedElfError:
            pass
        else:
            raise LinkedElfError("modified ELF unexpectedly passed: " + str(offset))
    try:
        check_linked_elf(data[:-1])
    except LinkedElfError:
        # Most linkers put the section-string table at the end of the file.
        pass
    print("FULL HOST-LINKED ELF SOURCE AND REFUSALS PASS; NO GUEST EXECUTION")


def emit(data: bytes) -> str:
    lines = []
    for offset in range(0, len(data), 16):
        row = ", ".join("std::byte{0x%02x}" % v for v in data[offset:offset + 16])
        lines.append("    " + row + ",")
    return (
        "#pragma once\n"
        "#include <array>\n"
        "#include <cstddef>\n"
        "namespace astraea::test::detail {\n"
        f"inline constexpr std::array<std::byte, {len(data)}> kOwnedLinkedElf{{\n"
        + "\n".join(lines)
        + "\n};\n}\n"
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--linked", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    data = args.linked.read_bytes()
    self_test(data)
    args.output.write_text(emit(data), encoding="ascii")


if __name__ == "__main__":
    main()
