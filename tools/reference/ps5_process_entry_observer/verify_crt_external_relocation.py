#!/usr/bin/env python3
"""Read-only proof that a pinned authored CRT entry needs an external symbol.

Inspect the independently compiled ELF64 x86-64 relocatable object only.
A relocation is neither a resolved provider nor a guest instruction executed.
No Sony runtime ABI assumption is introduced.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

MAX_OBJECT_BYTES = 2_000_000
MAX_SECTION_COUNT = 4096
MAX_SYMBOLS = 16384
MAX_RELOCATIONS = 16384
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_STRTAB = 3


class EvidenceError(ValueError):
    pass


def checked(data: bytes, offset: int, length: int) -> bytes:
    if offset < 0 or length < 0 or offset > len(data) or length > len(data) - offset:
        raise EvidenceError("ELF record outside bounded object")
    return data[offset : offset + length]


def unpack(fmt: str, data: bytes, offset: int) -> tuple:
    return struct.unpack(fmt, checked(data, offset, struct.calcsize(fmt)))


def cstring(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise EvidenceError("ELF symbol name offset outside string table")
    end = data.find(b"\x00", offset)
    if end < 0 or end - offset > 512:
        raise EvidenceError("missing or oversized symbol name")
    return data[offset:end].decode("ascii")


def inspect(data: bytes, entry_name: str, undefined_name: str) -> dict:
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise EvidenceError("expected ELF64 little-endian object")
    if unpack("<H", data, 16)[0] != 1 or unpack("<H", data, 18)[0] != 62:
        raise EvidenceError("expected x86-64 relocatable ELF, not a linked PS5 executable")
    shoff = unpack("<Q", data, 40)[0]
    shentsize, shnum = unpack("<HH", data, 58)
    if shentsize != 64 or not 1 < shnum <= MAX_SECTION_COUNT:
        raise EvidenceError("unsupported section header geometry")
    checked(data, shoff, shnum * shentsize)
    sections = []
    for i in range(shnum):
        offset = shoff + i * 64
        name, typ, flags, address, fileoff, size, link, info, align, entsize = unpack(
            "<IIQQQQIIQQ", data, offset)
        if typ != 8:
            checked(data, fileoff, size)
        sections.append(dict(type=typ, offset=fileoff, size=size,
                             link=link, info=info, entsize=entsize))
    symtabs = [i for i, s in enumerate(sections) if s["type"] == SHT_SYMTAB]
    if len(symtabs) != 1:
        raise EvidenceError("expected exactly one static symbol table")
    si = symtabs[0]
    symtab = sections[si]
    if symtab["entsize"] != 24 or symtab["size"] % 24:
        raise EvidenceError("invalid symbol table entry geometry")
    if not 0 < symtab["link"] < len(sections) or sections[symtab["link"]]["type"] != SHT_STRTAB:
        raise EvidenceError("invalid symbol string table")
    count = symtab["size"] // 24
    if not 0 < count <= MAX_SYMBOLS:
        raise EvidenceError("unexpected static symbol count")
    strings_section = sections[symtab["link"]]
    strings = checked(data, strings_section["offset"], strings_section["size"])
    symbols = []
    for i in range(count):
        name_off, info, other, shndx, value, size = unpack(
            "<IBBHQQ", data, symtab["offset"] + 24 * i)
        symbols.append(dict(name=cstring(strings, name_off), section=shndx,
                            value=value, size=size, kind=info & 15))
    starts = [s for s in symbols if s["name"] == entry_name and s["section"] != 0
              and s["kind"] == 2 and s["size"] > 0]
    targets = [i for i, s in enumerate(symbols)
               if s["name"] == undefined_name and s["section"] == 0]
    if len(starts) != 1 or len(targets) != 1:
        raise EvidenceError("missing or ambiguous defined entry / undefined requested dependency")
    start = starts[0]
    if start["section"] >= len(sections):
        raise EvidenceError("entry section out of bounds")
    if start["value"] + start["size"] > sections[start["section"]]["size"]:
        raise EvidenceError("entry function extends beyond its section")
    wanted = targets[0]
    matches = []
    for section in sections:
        if section["type"] != SHT_RELA or section["info"] != start["section"]:
            continue
        if section["link"] != si or section["entsize"] != 24 or section["size"] % 24:
            raise EvidenceError("invalid entry relocation section")
        number = section["size"] // 24
        if number > MAX_RELOCATIONS:
            raise EvidenceError("too many entry relocations")
        for i in range(number):
            offset, raw_info, addend = unpack(
                "<QQq", data, section["offset"] + 24 * i)
            sym_index = raw_info >> 32
            typ = raw_info & 0xffffffff
            if sym_index >= count:
                raise EvidenceError("relocation references missing static symbol")
            if sym_index == wanted:
                if offset < start["value"] or offset >= start["value"] + start["size"]:
                    raise EvidenceError("external dependency relocation lies outside entry function")
                matches.append((offset, typ, addend))
    if not 1 <= len(matches) <= 8:
        raise EvidenceError("no bounded relocation from compiled CRT entry to requested external")
    return {
        "policy": "static_object_relocation_only",
        "execution": "none",
        "guest_instructions": 0,
        "resolution": "not_attempted",
        "compiled_entry_symbol": entry_name,
        "undefined_startup_dependency_symbol": undefined_name,
        "entry_relocation_count": len(matches),
        "entry_relocation_types": sorted(set(typ for _, typ, _ in matches)),
        "relocation_offsets": [f"0x{offset:x}" for offset, _, _ in sorted(matches)],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--object", type=Path, required=True)
    ap.add_argument("--entry", default="astraea_reference_crt_start")
    ap.add_argument("--undefined", default="_init_env")
    args = ap.parse_args()
    if not (args.object.is_file() and 0 < args.object.stat().st_size <= MAX_OBJECT_BYTES):
        raise EvidenceError("missing/oversized pinned compiled CRT object")
    data = args.object.read_bytes()
    report = inspect(data, args.entry, args.undefined)
    report["object_sha256"] = hashlib.sha256(data).hexdigest()
    report["object_bytes"] = len(data)
    print("PINNED SOURCE-AUTHORED CRT RELOCATION PROOF (NO ENTRY)")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (EvidenceError, OSError, UnicodeError, struct.error) as exc:
        print(f"crt_relocation_error={exc}", file=sys.stderr)
        sys.exit(1)
