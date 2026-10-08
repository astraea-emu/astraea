#!/usr/bin/env python3
"""Derive the runtime identity of an owned PS5 PT_SCE_PROCPARAM observation.

This tool is intentionally narrow. It consumes two locally generated ELF files
from the pinned clean-room native-title toolchain plus numeric observations from
one controlled run. It does not deploy code, contact a console, or parse retail
content.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path

ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1
EM_X86_64 = 0x3E
SHT_SYMTAB = 2
SHT_NOBITS = 8
PT_LOAD = 1
PT_SCE_PROCPARAM = 0x61000001
ANCHOR_SYMBOL = "astraea_ps5_entry_capture_v0"
U64_MAX = (1 << 64) - 1
PREFIX_SIZE = 16


class AnalysisError(ValueError):
    pass


@dataclass(frozen=True)
class ElfHeader:
    phoff: int
    shoff: int
    phentsize: int
    phnum: int
    shentsize: int
    shnum: int


@dataclass(frozen=True)
class SectionHeader:
    section_type: int
    offset: int
    size: int
    link: int
    entsize: int


@dataclass(frozen=True)
class ProgramHeader:
    program_type: int
    offset: int
    vaddr: int
    filesz: int
    memsz: int


def _checked_slice(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise AnalysisError(f"{label} lies outside file")
    return data[offset : offset + size]


def _unpack_from(fmt: str, data: bytes, offset: int, label: str):
    size = struct.calcsize(fmt)
    chunk = _checked_slice(data, offset, size, label)
    return struct.unpack(fmt, chunk)


def _parse_elf_header(data: bytes, label: str) -> ElfHeader:
    if len(data) < 0x40:
        raise AnalysisError(f"{label}: truncated ELF header")
    if data[:4] != ELF_MAGIC:
        raise AnalysisError(f"{label}: bad ELF magic")
    if data[4] != ELFCLASS64:
        raise AnalysisError(f"{label}: not ELF64")
    if data[5] != ELFDATA2LSB:
        raise AnalysisError(f"{label}: not little-endian ELF")
    machine = _unpack_from("<H", data, 0x12, f"{label} machine")[0]
    if machine != EM_X86_64:
        raise AnalysisError(f"{label}: not x86-64")

    phoff = _unpack_from("<Q", data, 0x20, f"{label} phoff")[0]
    shoff = _unpack_from("<Q", data, 0x28, f"{label} shoff")[0]
    ehsize = _unpack_from("<H", data, 0x34, f"{label} ehsize")[0]
    phentsize = _unpack_from("<H", data, 0x36, f"{label} phentsize")[0]
    phnum = _unpack_from("<H", data, 0x38, f"{label} phnum")[0]
    shentsize = _unpack_from("<H", data, 0x3A, f"{label} shentsize")[0]
    shnum = _unpack_from("<H", data, 0x3C, f"{label} shnum")[0]

    if ehsize != 0x40:
        raise AnalysisError(f"{label}: unexpected ELF header size {ehsize}")
    if phnum and phentsize != 0x38:
        raise AnalysisError(f"{label}: unexpected program-header entry size {phentsize}")
    if shnum and shentsize != 0x40:
        raise AnalysisError(f"{label}: unexpected section-header entry size {shentsize}")

    if phnum:
        _checked_slice(
            data,
            phoff,
            phentsize * phnum,
            f"{label} program-header table",
        )
    if shnum:
        _checked_slice(
            data,
            shoff,
            shentsize * shnum,
            f"{label} section-header table",
        )

    return ElfHeader(
        phoff=phoff,
        shoff=shoff,
        phentsize=phentsize,
        phnum=phnum,
        shentsize=shentsize,
        shnum=shnum,
    )


def _section_headers(data: bytes, header: ElfHeader, label: str) -> list[SectionHeader]:
    result: list[SectionHeader] = []
    for index in range(header.shnum):
        at = header.shoff + index * header.shentsize
        (
            _name,
            section_type,
            _flags,
            _addr,
            offset,
            size,
            link,
            _info,
            _align,
            entsize,
        ) = _unpack_from("<IIQQQQIIQQ", data, at, f"{label} section {index}")
        # SHT_NOBITS (for example .bss) occupies memory, not file bytes.
        # Keep strict bounds checks for every file-backed section.
        if size and section_type != SHT_NOBITS:
            _checked_slice(data, offset, size, f"{label} section {index} contents")
        result.append(
            SectionHeader(
                section_type=section_type,
                offset=offset,
                size=size,
                link=link,
                entsize=entsize,
            )
        )
    return result


def _cstring(table: bytes, offset: int, label: str) -> str:
    if offset < 0 or offset >= len(table):
        raise AnalysisError(f"{label}: string offset outside table")
    end = table.find(b"\0", offset)
    if end < 0:
        raise AnalysisError(f"{label}: unterminated string")
    try:
        return table[offset:end].decode("utf-8")
    except UnicodeDecodeError as error:
        raise AnalysisError(f"{label}: invalid UTF-8 symbol name") from error


def find_defined_symbol(data: bytes, symbol: str, label: str) -> int:
    header = _parse_elf_header(data, label)
    sections = _section_headers(data, header, label)
    matches: list[int] = []

    for section_index, section in enumerate(sections):
        if section.section_type != SHT_SYMTAB:
            continue
        if section.entsize != 24 or section.size % section.entsize != 0:
            raise AnalysisError(f"{label}: malformed symbol table {section_index}")
        if section.link >= len(sections):
            raise AnalysisError(f"{label}: symbol table string link outside section table")
        strings = sections[section.link]
        string_data = _checked_slice(
            data,
            strings.offset,
            strings.size,
            f"{label} symbol strings",
        )

        for index in range(section.size // section.entsize):
            at = section.offset + index * section.entsize
            (
                name_offset,
                _info,
                _other,
                shndx,
                value,
                _size,
            ) = _unpack_from("<IBBHQQ", data, at, f"{label} symbol {index}")
            if shndx == 0:
                continue
            if _cstring(string_data, name_offset, f"{label} symbol {index}") == symbol:
                matches.append(value)

    if not matches:
        raise AnalysisError(f"{label}: required symbol {symbol!r} not found")
    if len(matches) != 1:
        raise AnalysisError(f"{label}: required symbol {symbol!r} is ambiguous")
    return matches[0]


def mapped_loads(data: bytes, label: str) -> list[ProgramHeader]:
    header = _parse_elf_header(data, label)
    result: list[ProgramHeader] = []

    for index in range(header.phnum):
        at = header.phoff + index * header.phentsize
        (
            program_type,
            _flags,
            offset,
            vaddr,
            _paddr,
            filesz,
            memsz,
            _align,
        ) = _unpack_from("<IIQQQQQQ", data, at, f"{label} program header {index}")
        if program_type != PT_LOAD or memsz == 0:
            continue
        if vaddr > U64_MAX - (memsz - 1):
            raise AnalysisError(f"{label}: PT_LOAD {index} virtual range overflows u64")
        result.append(
            ProgramHeader(
                program_type=program_type,
                offset=offset,
                vaddr=vaddr,
                filesz=filesz,
                memsz=memsz,
            )
        )

    return result


def require_unique_mapped_address(
    loads: list[ProgramHeader],
    address: int,
    label: str,
) -> None:
    matches = [
        load
        for load in loads
        if load.vaddr <= address < load.vaddr + load.memsz
    ]
    if not matches:
        raise AnalysisError(f"{label}: address is not inside a mapped PT_LOAD")
    if len(matches) != 1:
        raise AnalysisError(f"{label}: address is inside overlapping PT_LOAD ranges")


def find_procparam(data: bytes, label: str) -> tuple[ProgramHeader, bytes]:
    header = _parse_elf_header(data, label)
    matches: list[ProgramHeader] = []

    for index in range(header.phnum):
        at = header.phoff + index * header.phentsize
        (
            program_type,
            _flags,
            offset,
            vaddr,
            _paddr,
            filesz,
            memsz,
            _align,
        ) = _unpack_from("<IIQQQQQQ", data, at, f"{label} program header {index}")
        if program_type == PT_SCE_PROCPARAM:
            matches.append(
                ProgramHeader(
                    program_type=program_type,
                    offset=offset,
                    vaddr=vaddr,
                    filesz=filesz,
                    memsz=memsz,
                )
            )

    if not matches:
        raise AnalysisError(f"{label}: PT_SCE_PROCPARAM is missing")
    if len(matches) != 1:
        raise AnalysisError(f"{label}: PT_SCE_PROCPARAM is ambiguous")

    header_value = matches[0]
    if header_value.filesz < PREFIX_SIZE:
        raise AnalysisError(f"{label}: PT_SCE_PROCPARAM is smaller than {PREFIX_SIZE} bytes")
    if header_value.memsz < header_value.filesz:
        raise AnalysisError(f"{label}: PT_SCE_PROCPARAM memsz is smaller than filesz")

    prefix = _checked_slice(
        data,
        header_value.offset,
        PREFIX_SIZE,
        f"{label} PT_SCE_PROCPARAM prefix",
    )
    declared_size = struct.unpack_from("<Q", prefix, 0)[0]
    if declared_size != header_value.filesz:
        raise AnalysisError(
            f"{label}: procparam declared size 0x{declared_size:x} "
            f"does not match filesz 0x{header_value.filesz:x}"
        )
    if prefix[8:12] != b"ORBI":
        raise AnalysisError(f"{label}: procparam ORBI magic is missing")

    return header_value, prefix


def parse_u64(text: str, label: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as error:
        raise AnalysisError(f"{label}: invalid integer {text!r}") from error
    if not 0 <= value <= U64_MAX:
        raise AnalysisError(f"{label}: value is outside u64")
    return value


def parse_optional_u64(text: str | None, label: str) -> int | None:
    if text is None or text == "unknown":
        return None
    return parse_u64(text, label)


def parse_prefix(text: str, label: str = "prefix") -> bytes:
    if len(text) != PREFIX_SIZE * 2:
        raise AnalysisError(
            f"{label} must contain exactly {PREFIX_SIZE} bytes of hex"
        )
    try:
        value = bytes.fromhex(text)
    except ValueError as error:
        raise AnalysisError(f"{label} is not valid hexadecimal") from error
    if len(value) != PREFIX_SIZE:
        raise AnalysisError(f"{label} has the wrong size")
    return value


def parse_observation_line(line: str) -> dict[str, object]:
    parts = line.strip().split()
    if not parts or parts[0] != "ASTRAEA_ENTRY_V0":
        raise AnalysisError("observation line does not start with ASTRAEA_ENTRY_V0")

    fields: dict[str, str] = {}
    for token in parts[1:]:
        if "=" not in token:
            raise AnalysisError(f"malformed observation token {token!r}")
        key, value = token.split("=", 1)
        if key in fields:
            raise AnalysisError(f"duplicate observation field {key!r}")
        fields[key] = value

    if fields.get("status") != "complete":
        raise AnalysisError("observation status is not complete")

    required = (
        "capture_runtime",
        "rdi",
        "rsi",
        "rbp",
        "rsp",
        "process_prefix",
        "procparam_runtime",
        "procparam_prefix",
    )
    missing = [name for name in required if name not in fields]
    if missing:
        raise AnalysisError(
            "observation is missing required fields: " + ", ".join(missing)
        )

    procparam_runtime = parse_u64(
        fields["procparam_runtime"], "api procparam runtime"
    )
    if fields["procparam_prefix"] == "unavailable":
        if procparam_runtime != 0:
            raise AnalysisError(
                "procparam prefix may be unavailable only when the API return is null"
            )
        procparam_prefix = None
    else:
        procparam_prefix = parse_prefix(
            fields["procparam_prefix"], "api procparam prefix"
        )

    return {
        "capture_runtime": parse_u64(fields["capture_runtime"], "capture runtime"),
        "rdi": parse_u64(fields["rdi"], "rdi"),
        "rsi": parse_u64(fields["rsi"], "rsi"),
        "rbp": parse_u64(fields["rbp"], "rbp"),
        "rsp": parse_u64(fields["rsp"], "rsp"),
        "process_prefix": parse_prefix(fields["process_prefix"], "process prefix"),
        "api_procparam_runtime": procparam_runtime,
        "api_procparam_prefix": procparam_prefix,
    }


def parse_observation_log(text: str) -> dict[str, object]:
    matches = [
        line
        for line in text.splitlines()
        if line.strip().startswith("ASTRAEA_ENTRY_V0 ")
    ]
    complete = [line for line in matches if " status=complete" in line]
    if not complete:
        raise AnalysisError("log contains no complete ASTRAEA_ENTRY_V0 record")
    if len(complete) != 1:
        raise AnalysisError("log contains multiple complete ASTRAEA_ENTRY_V0 records")
    return parse_observation_line(complete[0])


def analyze(
    intermediate: bytes,
    final: bytes,
    capture_runtime: int,
    api_procparam_runtime: int,
    api_procparam_prefix: bytes | None,
    *,
    entry_observation: dict[str, object] | None = None,
    fs_base: int | None = None,
    gs_base: int | None = None,
) -> dict[str, object]:
    capture_link = find_defined_symbol(intermediate, ANCHOR_SYMBOL, "intermediate ELF")
    procparam, static_prefix = find_procparam(final, "final ELF")
    loads = mapped_loads(final, "final ELF")
    require_unique_mapped_address(
        loads,
        capture_link,
        "final ELF observer anchor",
    )
    require_unique_mapped_address(
        loads,
        procparam.vaddr,
        "final ELF PT_SCE_PROCPARAM",
    )

    if capture_runtime < capture_link:
        raise AnalysisError("observed capture runtime address is below its link-time address")
    load_bias = capture_runtime - capture_link
    if procparam.vaddr > U64_MAX - load_bias:
        raise AnalysisError("runtime procparam address overflows u64")
    expected_runtime = load_bias + procparam.vaddr

    result: dict[str, object] = {
        "schema": "astraea.ps5.procparam-identity/v0",
        "capture_link_vaddr": f"0x{capture_link:016x}",
        "capture_runtime_address": f"0x{capture_runtime:016x}",
        "load_bias": f"0x{load_bias:016x}",
        "procparam_link_vaddr": f"0x{procparam.vaddr:016x}",
        "expected_procparam_runtime": f"0x{expected_runtime:016x}",
        "observed_procparam_runtime": f"0x{api_procparam_runtime:016x}",
        "api_return_nonzero": api_procparam_runtime != 0,
        "pointer_match": api_procparam_runtime == expected_runtime,
        "prefix_available": api_procparam_prefix is not None,
        "static_prefix_hex": static_prefix.hex(),
        "observed_prefix_hex": (
            None
            if api_procparam_prefix is None
            else api_procparam_prefix.hex()
        ),
        "prefix_match": (
            False
            if api_procparam_prefix is None
            else api_procparam_prefix == static_prefix
        ),
        "external_thread_state": {
            "fs_base": None if fs_base is None else f"0x{fs_base:016x}",
            "gs_base": None if gs_base is None else f"0x{gs_base:016x}",
        },
    }
    if entry_observation is not None:
        startup_rdi = int(entry_observation["rdi"])
        process_prefix = bytes(entry_observation["process_prefix"])
        argc = int.from_bytes(process_prefix[0:4], "little")
        argv0 = int.from_bytes(process_prefix[8:16], "little")
        result["startup_parameters_distinct_from_api_return"] = (
            startup_rdi != api_procparam_runtime
        )
        result["entry_projection"] = {
            "argc": argc,
            "argv0_nonzero": argv0 != 0,
            "rsi_nonzero": int(entry_observation["rsi"]) != 0,
            "rbp_zero": int(entry_observation["rbp"]) == 0,
            "rsp_mod16": int(entry_observation["rsp"]) & 0xF,
            "fs_base_nonzero": None if fs_base is None else fs_base != 0,
            "gs_base_nonzero": None if gs_base is None else gs_base != 0,
        }
        result["entry_observation"] = {
            "rdi": f"0x{startup_rdi:016x}",
            "rsi": f"0x{int(entry_observation['rsi']):016x}",
            "rbp": f"0x{int(entry_observation['rbp']):016x}",
            "rsp": f"0x{int(entry_observation['rsp']):016x}",
            "process_prefix_hex": process_prefix.hex(),
        }
    return result


def structural_projection(result: dict[str, object]) -> dict[str, object]:
    entry = result.get("entry_projection")
    if not isinstance(entry, dict):
        raise AnalysisError("result does not contain an entry projection")
    if "startup_parameters_distinct_from_api_return" not in result:
        raise AnalysisError("result does not contain startup/procparam separation")

    return {
        "argc": entry["argc"],
        "argv0_nonzero": entry["argv0_nonzero"],
        "rsi_nonzero": entry["rsi_nonzero"],
        "rbp_zero": entry["rbp_zero"],
        "rsp_mod16": entry["rsp_mod16"],
        "fs_base_nonzero": entry["fs_base_nonzero"],
        "gs_base_nonzero": entry["gs_base_nonzero"],
        "api_return_nonzero": result["api_return_nonzero"],
        "pointer_match": result["pointer_match"],
        "prefix_available": result["prefix_available"],
        "prefix_match": result["prefix_match"],
        "startup_parameters_distinct_from_api_return":
            result["startup_parameters_distinct_from_api_return"],
    }


def compare_structural_results(
    first: dict[str, object],
    second: dict[str, object],
) -> dict[str, object]:
    first_projection = structural_projection(first)
    second_projection = structural_projection(second)

    order = (
        "argc",
        "argv0_nonzero",
        "rsi_nonzero",
        "rbp_zero",
        "rsp_mod16",
        "fs_base_nonzero",
        "gs_base_nonzero",
        "api_return_nonzero",
        "pointer_match",
        "prefix_available",
        "prefix_match",
        "startup_parameters_distinct_from_api_return",
    )

    difference = None
    for field in order:
        if first_projection[field] != second_projection[field]:
            difference = {
                "field": field,
                "first": first_projection[field],
                "second": second_projection[field],
            }
            break

    return {
        "schema": "astraea.ps5.process-entry-repeat/v0",
        "equivalent": difference is None,
        "first_difference": difference,
        "first_projection": first_projection,
        "second_projection": second_projection,
    }


def _elf_header(*, phoff: int, phnum: int, shoff: int, shnum: int) -> bytearray:
    data = bytearray(0x40)
    data[0:4] = ELF_MAGIC
    data[4] = ELFCLASS64
    data[5] = ELFDATA2LSB
    data[6] = 1
    struct.pack_into("<H", data, 0x10, 3)
    struct.pack_into("<H", data, 0x12, EM_X86_64)
    struct.pack_into("<I", data, 0x14, 1)
    struct.pack_into("<Q", data, 0x20, phoff)
    struct.pack_into("<Q", data, 0x28, shoff)
    struct.pack_into("<H", data, 0x34, 0x40)
    struct.pack_into("<H", data, 0x36, 0x38)
    struct.pack_into("<H", data, 0x38, phnum)
    struct.pack_into("<H", data, 0x3A, 0x40)
    struct.pack_into("<H", data, 0x3C, shnum)
    return data


def _synthetic_intermediate(
    *,
    duplicate_symbol: bool = False,
    unbacked_section_type: int | None = None,
) -> bytes:
    shoff = 0x100
    strtab_offset = 0x240
    symtab_offset = 0x280
    symbol_name = ANCHOR_SYMBOL.encode("utf-8")
    strings = b"\0" + symbol_name + b"\0"
    symbol_count = 3 if duplicate_symbol else 2
    symtab_size = symbol_count * 24
    section_count = 4 if unbacked_section_type is not None else 3
    total = max(
        shoff + section_count * 0x40,
        strtab_offset + len(strings),
        symtab_offset + symtab_size,
    )
    data = _elf_header(phoff=0, phnum=0, shoff=shoff, shnum=section_count)
    data.extend(b"\0" * (total - len(data)))

    # Section 1: string table.
    struct.pack_into(
        "<IIQQQQIIQQ",
        data,
        shoff + 0x40,
        0,
        3,
        0,
        0,
        strtab_offset,
        len(strings),
        0,
        0,
        1,
        0,
    )
    data[strtab_offset : strtab_offset + len(strings)] = strings

    # Section 2: symbol table linked to section 1.
    struct.pack_into(
        "<IIQQQQIIQQ",
        data,
        shoff + 0x80,
        0,
        SHT_SYMTAB,
        0,
        0,
        symtab_offset,
        symtab_size,
        1,
        0,
        8,
        24,
    )
    capture_section = 3 if unbacked_section_type == SHT_NOBITS else 1
    struct.pack_into(
        "<IBBHQQ", data, symtab_offset + 24,
        1, 0x11, 0, capture_section, 0x5000, 0xA0,
    )
    if duplicate_symbol:
        struct.pack_into("<IBBHQQ", data, symtab_offset + 48, 1, 0x11, 0, 1, 0x6000, 0xA0)
    if unbacked_section_type is not None:
        # An unbacked .bss section is valid; a same-sized PROGBITS section
        # pointing beyond EOF must still be rejected.
        struct.pack_into(
            "<IIQQQQIIQQ",
            data,
            shoff + 3 * 0x40,
            0, unbacked_section_type, 0, 0, total, 0xA0, 0, 0, 16, 0,
        )
    return bytes(data)


def _synthetic_final(
    *,
    duplicate_procparam: bool = False,
    corrupt_magic: bool = False,
    omit_load: bool = False,
    overlap_load: bool = False,
    procparam_unmapped: bool = False,
) -> bytes:
    procparam_count = 2 if duplicate_procparam else 1
    load_count = 0 if omit_load else (2 if overlap_load else 1)
    phnum = load_count + procparam_count
    phoff = 0x40
    payload_offset = 0x180
    second_offset = 0x200
    total = second_offset + 0x60 if duplicate_procparam else payload_offset + 0x60
    data = _elf_header(phoff=phoff, phnum=phnum, shoff=0, shnum=0)
    data.extend(b"\0" * (total - len(data)))

    index = 0
    if not omit_load:
        struct.pack_into(
            "<IIQQQQQQ",
            data,
            phoff + index * 0x38,
            PT_LOAD,
            6,
            0x100,
            0x4000,
            0x4000,
            0x100,
            0x2000 if procparam_unmapped else 0x5000,
            0x1000,
        )
        index += 1
        if overlap_load:
            struct.pack_into(
                "<IIQQQQQQ",
                data,
                phoff + index * 0x38,
                PT_LOAD,
                4,
                0x120,
                0x4800,
                0x4800,
                0x80,
                0x2000,
                0x1000,
            )
            index += 1

    def write_procparam(header_index: int, offset: int, vaddr: int) -> None:
        struct.pack_into(
            "<IIQQQQQQ",
            data,
            phoff + header_index * 0x38,
            PT_SCE_PROCPARAM,
            4,
            offset,
            vaddr,
            vaddr,
            0x60,
            0x60,
            8,
        )
        struct.pack_into("<Q", data, offset, 0x60)
        data[offset + 8 : offset + 12] = b"NOPE" if corrupt_magic else b"ORBI"

    write_procparam(index, payload_offset, 0x7000)
    if duplicate_procparam:
        write_procparam(index + 1, second_offset, 0x8000)
    return bytes(data)


class SelfTests(unittest.TestCase):
    def test_success(self) -> None:
        final = _synthetic_final()
        static_prefix = final[0x180 : 0x190]
        result = analyze(
            _synthetic_intermediate(),
            final,
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=static_prefix,
        )
        self.assertTrue(result["api_return_nonzero"])
        self.assertTrue(result["pointer_match"])
        self.assertTrue(result["prefix_available"])
        self.assertTrue(result["prefix_match"])
        self.assertEqual(result["load_bias"], "0x0000000010000000")

    def test_unbacked_nobits_capture_is_valid(self) -> None:
        final = _synthetic_final()
        result = analyze(
            _synthetic_intermediate(unbacked_section_type=SHT_NOBITS),
            final,
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=final[0x180 : 0x190],
        )
        self.assertTrue(result["pointer_match"])
        self.assertTrue(result["prefix_match"])

    def test_truncated_file_backed_section_remains_rejected(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "section 3 contents"):
            find_defined_symbol(
                _synthetic_intermediate(unbacked_section_type=1),
                ANCHOR_SYMBOL,
                "intermediate ELF",
            )

    def test_pointer_mismatch_is_observation(self) -> None:
        final = _synthetic_final()
        result = analyze(
            _synthetic_intermediate(),
            final,
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007008,
            api_procparam_prefix=final[0x180 : 0x190],
        )
        self.assertFalse(result["pointer_match"])
        self.assertTrue(result["prefix_match"])

    def test_prefix_mismatch_is_observation(self) -> None:
        result = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=b"\0" * PREFIX_SIZE,
        )
        self.assertTrue(result["pointer_match"])
        self.assertFalse(result["prefix_match"])

    def test_duplicate_anchor_is_rejected(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "ambiguous"):
            analyze(
                _synthetic_intermediate(duplicate_symbol=True),
                _synthetic_final(),
                capture_runtime=0x10005000,
                api_procparam_runtime=0x10007000,
                api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            )

    def test_duplicate_procparam_is_rejected(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "ambiguous"):
            analyze(
                _synthetic_intermediate(),
                _synthetic_final(duplicate_procparam=True),
                capture_runtime=0x10005000,
                api_procparam_runtime=0x10007000,
                api_procparam_prefix=b"\0" * PREFIX_SIZE,
            )

    def test_bad_procparam_magic_is_rejected(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "ORBI"):
            analyze(
                _synthetic_intermediate(),
                _synthetic_final(corrupt_magic=True),
                capture_runtime=0x10005000,
                api_procparam_runtime=0x10007000,
                api_procparam_prefix=b"\0" * PREFIX_SIZE,
            )

    def test_observation_log_parses_machine_record(self) -> None:
        observed = parse_observation_log(
            "noise before\n"
            "ASTRAEA_ENTRY_V0 status=complete "
            "capture_runtime=0x0000000010005000 "
            "rdi=0x0000000000001111 "
            "rsi=0x0000000000002222 "
            "rbp=0x0000000000003333 "
            "rsp=0x0000000000004448 "
            "process_prefix=000102030405060708090a0b0c0d0e0f "
            "procparam_runtime=0x0000000010007000 "
            "procparam_prefix=60000000000000004f52424900000000\n"
        )
        self.assertEqual(observed["capture_runtime"], 0x10005000)
        self.assertEqual(observed["rsi"], 0x2222)
        self.assertEqual(observed["process_prefix"], bytes(range(16)))

    def test_log_record_drives_identity_analysis(self) -> None:
        final = _synthetic_final()
        observed = parse_observation_log(
            "ASTRAEA_ENTRY_V0 status=complete "
            "capture_runtime=0x0000000010005000 "
            "rdi=0x0000000000001111 "
            "rsi=0x0000000000002222 "
            "rbp=0x0000000000003333 "
            "rsp=0x0000000000004448 "
            "process_prefix=000102030405060708090a0b0c0d0e0f "
            "procparam_runtime=0x0000000010007000 "
            "procparam_prefix=60000000000000004f52424900000000\n"
        )
        result = analyze(
            _synthetic_intermediate(),
            final,
            int(observed["capture_runtime"]),
            int(observed["api_procparam_runtime"]),
            bytes(observed["api_procparam_prefix"]),
            entry_observation=observed,
        )
        self.assertTrue(result["pointer_match"])
        self.assertTrue(result["prefix_match"])
        self.assertTrue(
            result["startup_parameters_distinct_from_api_return"]
        )
        self.assertEqual(
            result["entry_projection"],
            {
                "argc": 0x03020100,
                "argv0_nonzero": True,
                "rsi_nonzero": True,
                "rbp_zero": False,
                "rsp_mod16": 8,
                "fs_base_nonzero": None,
                "gs_base_nonzero": None,
            },
        )
        self.assertEqual(
            result["entry_observation"]["rsp"],
            "0x0000000000004448",
        )

    def test_null_procparam_return_is_preserved_as_observation(self) -> None:
        final = _synthetic_final()
        result = analyze(
            _synthetic_intermediate(),
            final,
            capture_runtime=0x10005000,
            api_procparam_runtime=0,
            api_procparam_prefix=None,
        )
        self.assertFalse(result["api_return_nonzero"])
        self.assertFalse(result["pointer_match"])
        self.assertFalse(result["prefix_available"])
        self.assertFalse(result["prefix_match"])
        self.assertIsNone(result["observed_prefix_hex"])

    def test_null_procparam_log_record_is_preserved(self) -> None:
        observed = parse_observation_log(
            "ASTRAEA_ENTRY_V0 status=complete "
            "capture_runtime=0x0000000010005000 "
            "rdi=0x0000000000001111 "
            "rsi=0x0000000000002222 "
            "rbp=0x0000000000003333 "
            "rsp=0x0000000000004448 "
            "process_prefix=000102030405060708090a0b0c0d0e0f "
            "procparam_runtime=0x0000000000000000 "
            "procparam_prefix=unavailable\n"
        )
        self.assertEqual(observed["api_procparam_runtime"], 0)
        self.assertIsNone(observed["api_procparam_prefix"])

    def test_unavailable_prefix_with_nonzero_pointer_is_rejected(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "null"):
            parse_observation_line(
                "ASTRAEA_ENTRY_V0 status=complete "
                "capture_runtime=0x0000000010005000 "
                "rdi=0x0000000000001111 "
                "rsi=0x0000000000002222 "
                "rbp=0x0000000000003333 "
                "rsp=0x0000000000004448 "
                "process_prefix=000102030405060708090a0b0c0d0e0f "
                "procparam_runtime=0x0000000010007000 "
                "procparam_prefix=unavailable"
            )

    def test_duplicate_complete_log_record_is_rejected(self) -> None:
        line = (
            "ASTRAEA_ENTRY_V0 status=complete "
            "capture_runtime=0x0000000010005000 "
            "rdi=0x0000000000001111 "
            "rsi=0x0000000000002222 "
            "rbp=0x0000000000003333 "
            "rsp=0x0000000000004448 "
            "process_prefix=000102030405060708090a0b0c0d0e0f "
            "procparam_runtime=0x0000000010007000 "
            "procparam_prefix=60000000000000004f52424900000000"
        )
        with self.assertRaisesRegex(AnalysisError, "multiple"):
            parse_observation_log(line + "\n" + line + "\n")

    def test_final_anchor_must_be_mapped(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "observer anchor"):
            analyze(
                _synthetic_intermediate(),
                _synthetic_final(omit_load=True),
                capture_runtime=0x10005000,
                api_procparam_runtime=0x10007000,
                api_procparam_prefix=b"\0" * PREFIX_SIZE,
            )

    def test_overlapping_final_loads_are_rejected(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "overlapping PT_LOAD"):
            analyze(
                _synthetic_intermediate(),
                _synthetic_final(overlap_load=True),
                capture_runtime=0x10005000,
                api_procparam_runtime=0x10007000,
                api_procparam_prefix=b"\0" * PREFIX_SIZE,
            )

    def test_final_procparam_must_be_mapped(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "PT_SCE_PROCPARAM"):
            analyze(
                _synthetic_intermediate(),
                _synthetic_final(procparam_unmapped=True),
                capture_runtime=0x10005000,
                api_procparam_runtime=0x10007000,
                api_procparam_prefix=b"\0" * PREFIX_SIZE,
            )

    def test_repeat_comparison_ignores_aslr_sensitive_addresses(self) -> None:
        first = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation={
                "rdi": 0x10001111,
                "rsi": 0x10002222,
                "rbp": 0,
                "rsp": 0x10004448,
                "process_prefix": bytes(range(16)),
            },
        )
        second = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x20005000,
            api_procparam_runtime=0x20007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation={
                "rdi": 0x20001111,
                "rsi": 0x20002222,
                "rbp": 0,
                "rsp": 0x20004448,
                "process_prefix": bytes(range(16)),
            },
        )
        comparison = compare_structural_results(first, second)
        self.assertTrue(comparison["equivalent"])
        self.assertIsNone(comparison["first_difference"])

    def test_repeat_comparison_reports_first_structural_difference(self) -> None:
        first = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation={
                "rdi": 0x10001111,
                "rsi": 0x10002222,
                "rbp": 0,
                "rsp": 0x10004448,
                "process_prefix": bytes(range(16)),
            },
        )
        second = dict(first)
        second["entry_projection"] = dict(first["entry_projection"])
        second["entry_projection"]["rsp_mod16"] = 0
        comparison = compare_structural_results(first, second)
        self.assertFalse(comparison["equivalent"])
        self.assertEqual(
            comparison["first_difference"],
            {"field": "rsp_mod16", "first": 8, "second": 0},
        )

    def test_external_segment_bases_preserve_unknown_zero_and_nonzero(self) -> None:
        observed = {
            "rdi": 0x10001111,
            "rsi": 0x10002222,
            "rbp": 0,
            "rsp": 0x10004448,
            "process_prefix": bytes(range(16)),
        }
        unknown = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation=observed,
        )
        explicit = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation=observed,
            fs_base=0,
            gs_base=0x12345000,
        )
        self.assertIsNone(unknown["entry_projection"]["fs_base_nonzero"])
        self.assertIsNone(unknown["entry_projection"]["gs_base_nonzero"])
        self.assertFalse(explicit["entry_projection"]["fs_base_nonzero"])
        self.assertTrue(explicit["entry_projection"]["gs_base_nonzero"])
        self.assertEqual(explicit["external_thread_state"]["fs_base"], "0x0000000000000000")
        self.assertEqual(explicit["external_thread_state"]["gs_base"], "0x0000000012345000")

    def test_repeat_comparison_ignores_changed_nonzero_segment_base_addresses(self) -> None:
        observed = {
            "rdi": 0x10001111,
            "rsi": 0x10002222,
            "rbp": 0,
            "rsp": 0x10004448,
            "process_prefix": bytes(range(16)),
        }
        first = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation=observed,
            fs_base=0x11110000,
            gs_base=0,
        )
        second = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x20005000,
            api_procparam_runtime=0x20007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation={
                **observed,
                "rdi": 0x20001111,
                "rsi": 0x20002222,
                "rsp": 0x20004448,
            },
            fs_base=0x22220000,
            gs_base=0,
        )
        comparison = compare_structural_results(first, second)
        self.assertTrue(comparison["equivalent"])
        self.assertIsNone(comparison["first_difference"])

    def test_repeat_comparison_reports_segment_base_state_change(self) -> None:
        observed = {
            "rdi": 0x10001111,
            "rsi": 0x10002222,
            "rbp": 0,
            "rsp": 0x10004448,
            "process_prefix": bytes(range(16)),
        }
        first = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation=observed,
            fs_base=0,
        )
        second = analyze(
            _synthetic_intermediate(),
            _synthetic_final(),
            capture_runtime=0x10005000,
            api_procparam_runtime=0x10007000,
            api_procparam_prefix=_synthetic_final()[0x180 : 0x190],
            entry_observation=observed,
            fs_base=0x33330000,
        )
        comparison = compare_structural_results(first, second)
        self.assertFalse(comparison["equivalent"])
        self.assertEqual(
            comparison["first_difference"],
            {"field": "fs_base_nonzero", "first": False, "second": True},
        )

    def test_malformed_elf_is_rejected(self) -> None:
        with self.assertRaisesRegex(AnalysisError, "bad ELF magic"):
            find_defined_symbol(b"not-an-elf" + b"\0" * 64, ANCHOR_SYMBOL, "bad")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--intermediate", type=Path)
    parser.add_argument("--final", dest="final_elf", type=Path)
    parser.add_argument("--capture-runtime")
    parser.add_argument("--api-procparam-runtime")
    parser.add_argument("--api-procparam-prefix")
    parser.add_argument(
        "--fs-base",
        help="optional externally observed initial FS base; omit or use unknown",
    )
    parser.add_argument(
        "--gs-base",
        help="optional externally observed initial GS base; omit or use unknown",
    )
    parser.add_argument(
        "--log-file",
        type=Path,
        help="read capture/procparam observations from one emitted log record",
    )
    parser.add_argument(
        "--compare-log-files",
        nargs=2,
        type=Path,
        metavar=("RUN1", "RUN2"),
        help="analyze two emitted log records and compare only structural facts",
    )
    parser.add_argument(
        "--compare-fs-bases",
        nargs=2,
        metavar=("RUN1_FS", "RUN2_FS"),
        help="optional external FS bases for the two compared runs; use unknown when absent",
    )
    parser.add_argument(
        "--compare-gs-bases",
        nargs=2,
        metavar=("RUN1_GS", "RUN2_GS"),
        help="optional external GS bases for the two compared runs; use unknown when absent",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)

    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(SelfTests)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        return 0 if result.wasSuccessful() else 1

    required_files = {
        "--intermediate": args.intermediate,
        "--final": args.final_elf,
    }
    missing_files = [
        name for name, value in required_files.items() if value is None
    ]
    if missing_files:
        raise AnalysisError(
            "missing required arguments: " + ", ".join(missing_files)
        )

    if args.compare_log_files is not None:
        if args.log_file is not None or any(
            value is not None
            for value in (
                args.capture_runtime,
                args.api_procparam_runtime,
                args.api_procparam_prefix,
                args.fs_base,
                args.gs_base,
            )
        ):
            raise AnalysisError(
                "--compare-log-files cannot be combined with other observation arguments"
            )

        intermediate = args.intermediate.read_bytes()
        final = args.final_elf.read_bytes()
        fs_bases = (
            [None, None]
            if args.compare_fs_bases is None
            else [
                parse_optional_u64(value, f"run {index + 1} FS base")
                for index, value in enumerate(args.compare_fs_bases)
            ]
        )
        gs_bases = (
            [None, None]
            if args.compare_gs_bases is None
            else [
                parse_optional_u64(value, f"run {index + 1} GS base")
                for index, value in enumerate(args.compare_gs_bases)
            ]
        )
        results = []
        for index, path in enumerate(args.compare_log_files):
            observed = parse_observation_log(
                path.read_text(encoding="utf-8")
            )
            raw_prefix = observed["api_procparam_prefix"]
            results.append(
                analyze(
                    intermediate,
                    final,
                    int(observed["capture_runtime"]),
                    int(observed["api_procparam_runtime"]),
                    None if raw_prefix is None else bytes(raw_prefix),
                    entry_observation=observed,
                    fs_base=fs_bases[index],
                    gs_base=gs_bases[index],
                )
            )
        print(
            json.dumps(
                compare_structural_results(results[0], results[1]),
                indent=2,
                sort_keys=True,
            )
        )
        return 0

    if args.compare_fs_bases is not None or args.compare_gs_bases is not None:
        raise AnalysisError(
            "comparison-only segment-base arguments require --compare-log-files"
        )

    fs_base = parse_optional_u64(args.fs_base, "FS base")
    gs_base = parse_optional_u64(args.gs_base, "GS base")

    entry_observation = None
    if args.log_file is not None:
        manual = (
            args.capture_runtime,
            args.api_procparam_runtime,
            args.api_procparam_prefix,
        )
        if any(value is not None for value in manual):
            raise AnalysisError(
                "--log-file cannot be combined with manual observation arguments"
            )
        entry_observation = parse_observation_log(
            args.log_file.read_text(encoding="utf-8")
        )
        capture_runtime = int(entry_observation["capture_runtime"])
        api_procparam_runtime = int(
            entry_observation["api_procparam_runtime"]
        )
        raw_prefix = entry_observation["api_procparam_prefix"]
        api_procparam_prefix = (
            None if raw_prefix is None else bytes(raw_prefix)
        )
    else:
        required_observations = {
            "--capture-runtime": args.capture_runtime,
            "--api-procparam-runtime": args.api_procparam_runtime,
        }
        missing = [
            name
            for name, value in required_observations.items()
            if value is None
        ]
        if missing:
            raise AnalysisError(
                "missing required arguments: " + ", ".join(missing)
            )
        capture_runtime = parse_u64(
            args.capture_runtime, "capture runtime"
        )
        api_procparam_runtime = parse_u64(
            args.api_procparam_runtime, "api procparam runtime"
        )
        if api_procparam_runtime == 0:
            if args.api_procparam_prefix not in (None, "unavailable"):
                raise AnalysisError(
                    "--api-procparam-prefix must be omitted or unavailable "
                    "when the API return is null"
                )
            api_procparam_prefix = None
        else:
            if args.api_procparam_prefix is None:
                raise AnalysisError(
                    "missing required argument: --api-procparam-prefix"
                )
            api_procparam_prefix = parse_prefix(
                args.api_procparam_prefix,
                "api procparam prefix",
            )

    intermediate = args.intermediate.read_bytes()
    final = args.final_elf.read_bytes()
    result = analyze(
        intermediate,
        final,
        capture_runtime,
        api_procparam_runtime,
        api_procparam_prefix,
        entry_observation=entry_observation,
        fs_base=fs_base,
        gs_base=gs_base,
    )
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AnalysisError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
