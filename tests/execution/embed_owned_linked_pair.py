#!/usr/bin/env python3
"""Check two independently linked ELF64 modules and emit test-only source bytes.

The verifier records an explicit bounded list of fields requiring relocation
when the test supervisor chooses a free host guest address. It neither
executes the ELFs nor infers a Sony module ABI or provider.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import struct

IMPORT = b"ABCDEFGHIJK#owned-lib#owned-provider"
MAX_BYTES = 65536
ADDRESS_TAGS = {3, 4, 5, 6, 7, 12, 13, 17, 23, 25, 26, 32}
CLIENT_CODE_PREFIX = bytes.fromhex("48bf2a00000000000000e8")
PROVIDER_CODE = bytes.fromhex("48b80000000000000000ffe0")


class FixtureError(ValueError):
    pass


def checked(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise FixtureError("ELF field out of bounds")
    return data[offset:offset + size]


def unpack(fmt: str, data: bytes, offset: int):
    return struct.unpack(fmt, checked(data, offset, struct.calcsize(fmt)))


def elf(data: bytes, *, client: bool) -> dict:
    if not 64 <= len(data) <= MAX_BYTES or data[:7] != b"\x7fELF\x02\x01\x01":
        raise FixtureError("expected bounded little-endian ELF64 input")
    if unpack("<HHI", data, 16) != (3, 62, 1):
        raise FixtureError("expected AMD64 shared ELF with ordinary dynamic linking")
    entry, phoff, shoff = unpack("<QQQ", data, 24)
    ehsize, phentsize, phnum, shentsize, shnum, shstr = unpack("<HHHHHH", data, 52)
    if (ehsize, phentsize) != (64, 56) or not 3 <= phnum <= 12:
        raise FixtureError("unexpected ELF program header count/geometry")
    if shentsize != 64 or not 4 <= shnum <= 24 or shstr >= shnum:
        raise FixtureError("unexpected ELF section header count/geometry")
    checked(data, phoff, 56 * phnum)
    checked(data, shoff, 64 * shnum)
    ph = [unpack("<IIQQQQQQ", data, phoff + 56 * i) for i in range(phnum)]
    sec = [unpack("<IIQQQQIIQQ", data, shoff + 64 * i) for i in range(shnum)]
    loads = [p for p in ph if p[0] == 1 and p[5]]
    if len(loads) != 3 or not any(p[1] == 5 for p in loads):
        raise FixtureError("expected exact source-built read/execute/write load subset")
    if any(p[1] == 7 or p[5] > p[6] or p[7] != 4096 for p in loads):
        raise FixtureError("unsupported ELF LOAD permissions/size/alignment")
    if any(p[3] % p[7] != p[2] % p[7] for p in loads):
        raise FixtureError("misaligned linked PT_LOAD")
    if any(loads[i][3] + loads[i][6] > loads[i + 1][3] for i in range(len(loads) - 1)):
        raise FixtureError("overlapping linked PT_LOAD segments")
    dynamic = [p for p in ph if p[0] == 2]
    if len(dynamic) != 1 or dynamic[0][5] % 16:
        raise FixtureError("expected one ordinary PT_DYNAMIC")
    if entry == 0:
        raise FixtureError("unexpected linked entry-point policy")
    fixups = {24}
    for i, p in enumerate(ph):
        if p[0] in (1, 2, 0x6474e552):
            fixups.add(phoff + 56 * i + 16)
            fixups.add(phoff + 56 * i + 24)

    def mapped_file(va: int, size: int) -> int:
        for p in loads:
            if p[3] <= va and size <= p[5] and va - p[3] <= p[5] - size:
                return p[2] + va - p[3]
        raise FixtureError("dynamic address is outside file-backed LOAD")

    tags = {}
    for i in range(dynamic[0][5] // 16):
        at = dynamic[0][2] + i * 16
        tag, value = unpack("<qQ", data, at)
        if tag == 0:
            break
        if tag in tags:
            raise FixtureError("duplicate ELF dynamic tag")
        tags[tag] = value
        if tag in ADDRESS_TAGS and value:
            mapped_file(value, 1)
            fixups.add(at + 8)
    else:
        raise FixtureError("missing DT_NULL")
    required = {4, 5, 6, 10, 11}
    if not required.issubset(tags) or tags[11] != 24:
        raise FixtureError("incomplete ELF dynamic symbol metadata")
    strtab = mapped_file(tags[5], tags[10])
    strings = checked(data, strtab, tags[10])
    hash_off = mapped_file(tags[4], 8)
    _buckets, count = unpack("<II", data, hash_off)
    if not 2 <= count <= 16:
        raise FixtureError("unexpected SysV dynamic symbol count")
    syms = mapped_file(tags[6], count * 24)

    def get_name(offset: int) -> bytes:
        if offset >= len(strings):
            raise FixtureError("symbol string offset out of bounds")
        end = strings.find(b"\0", offset)
        if end < 0:
            raise FixtureError("unterminated symbol string")
        return strings[offset:end]

    imported, exported = [], []
    for i in range(count):
        at = syms + 24 * i
        name, info, other, shndx, value, size = unpack("<IBBHQQ", data, at)
        name_bytes = get_name(name)
        if shndx and value:
            mapped_file(value, 1)
            fixups.add(at + 8)
        if name_bytes == IMPORT and shndx == 0 and i != 0:
            imported.append((i, info))
        if name_bytes == IMPORT and shndx != 0:
            exported.append((i, info, value, size))
    if client and (len(imported) != 1 or exported):
        raise FixtureError("client must import exactly the source-authored long-form symbol")
    if not client and (imported or len(exported) != 1 or exported[0][1] != 0x12):
        raise FixtureError("provider must define exactly the named executable export")
    if client:
        for tag in (2, 3, 20, 23):
            if tag not in tags:
                raise FixtureError("missing PLT relocation companion tag")
        if tags[2] != 24 or tags[20] != 7:
            raise FixtureError("expected one RELA PLT relocation")
        at = mapped_file(tags[23], 24)
        target, info, addend = unpack("<QQq", data, at)
        if info != (imported[0][0] << 32) | 7 or addend != 0:
            raise FixtureError("wrong linked JUMP_SLOT index, type or addend")
        mapped_file(target, 8)
        fixups.add(at)
        got = target
    else:
        if 23 in tags or 2 in tags:
            raise FixtureError("provider unexpectedly imports through PLT")
        got = 0
    if any(h[1] in (4, 9) and h[5] and h[1] == 9 for h in sec):
        raise FixtureError("unexpected REL-form relocation")
    for sh in sec:
        if sh[2] & 2 and sh[3]:
            fixups.add(shoff + sec.index(sh) * 64 + 16)
    # Keep the ordinary static symbol table coherent with its mapped source
    # definitions. Undefined symbol entries must not be rebased.
    for sh in sec:
        if sh[1] != 2:
            continue
        if sh[9] != 24 or sh[5] % 24:
            raise FixtureError("invalid source static symbol table")
        for i in range(sh[5] // 24):
            at = sh[4] + 24 * i
            _name, _info, _other, shndx, value, _size = unpack("<IBBHQQ", data, at)
            if shndx and value:
                fixups.add(at + 8)

    # Actual machine code is linked and frozen, not assembled at runtime.
    code_off = mapped_file(entry if client else exported[0][2],
                           17 if client else len(PROVIDER_CODE))
    code = checked(data, code_off, 17 if client else len(PROVIDER_CODE))
    if client:
        if not code.startswith(CLIENT_CODE_PREFIX) or code[-2:] != b"\x0f\x0b":
            raise FixtureError("unexpected linked PLT call instruction")
    elif code != PROVIDER_CODE or exported[0][3] != len(PROVIDER_CODE):
        raise FixtureError("unexpected independently linked provider code")
    # A GNU ld .got.plt reserves a pointer to .dynamic. That pointer must
    # remain valid for structural consistency after guest-VMA relocation.
    if client:
        got_base = tags[3]
        first = mapped_file(got_base, 8)
        ptr = unpack("<Q", data, first)[0]
        if ptr != dynamic[0][3]:
            raise FixtureError("unknown linker GOT dynamic-pointer convention")
        fixups.add(first)
    return {
        "bytes": data, "fixups": sorted(fixups),
        "entry": entry if client else exported[0][2],
        "got": got, "code_offset": code_off,
        "export_index": exported[0][0] if exported else 0,
        "import_index": imported[0][0] if imported else 0,
    }


def emit(name: str, data: bytes, fixups: list[int]) -> str:
    rows = []
    for i in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"std::byte{{0x{b:02x}}}" for b in data[i:i + 16]) + ",")
    offsets = ", ".join(str(x) + "U" for x in fixups)
    return (
        f"inline constexpr std::array<std::byte, {len(data)}> k{name}Elf{{\n"
        + "\n".join(rows) + "\n};\n"
        + f"inline constexpr std::array<std::size_t, {len(fixups)}> k{name}RelocationFields{{{offsets}}};\n"
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--client", type=Path, required=True)
    ap.add_argument("--provider", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    c = elf(args.client.read_bytes(), client=True)
    p = elf(args.provider.read_bytes(), client=False)
    if c["entry"] == p["entry"] or not c["got"]:
        raise FixtureError("linked pair lacks distinct entry and JUMP_SLOT")
    out = (
        "#pragma once\n#include <array>\n#include <cstddef>\n\n"
        "namespace astraea::test::detail {\n"
        + emit("Client", c["bytes"], c["fixups"])
        + emit("Provider", p["bytes"], p["fixups"])
        + f"inline constexpr std::uint64_t kClientEntryOffset = {c['entry']}ULL;\n"
        + f"inline constexpr std::uint64_t kClientGotOffset = {c['got']}ULL;\n"
        + f"inline constexpr std::uint64_t kProviderEntryOffset = {p['entry']}ULL;\n"
        + f"inline constexpr std::size_t kProviderGateImmediateOffset = {p['code_offset'] + 2}U;\n"
        + f"inline constexpr std::uint64_t kClientImportSymbolIndex = {c['import_index']}ULL;\n"
        + f"inline constexpr std::uint64_t kProviderExportSymbolIndex = {p['export_index']}ULL;\n"
        "}\n"
    )
    args.output.write_text(out, encoding="ascii")
    print("TWO COMPLETE SOURCE-LINKED ELF IMPORT/EXPORT IDENTITIES VERIFIED; NO GUEST EXECUTION")


if __name__ == "__main__":
    main()
