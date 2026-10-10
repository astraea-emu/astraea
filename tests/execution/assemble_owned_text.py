#!/usr/bin/env python3
"""Extract strictly checked assembly output for the owned two-ELF worker test.

The generated code is placed into test-only PS5-shaped ELF envelopes by the
existing fixture, then run under the existing supervised Linux worker. This
is not a source-built final ELF, a Sony ABI profile or retail admission.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct


def checked(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError("ELF field out of bounds")
    return data[offset:offset + size]


def decode_object(path: Path, symbol_name: str) -> bytes:
    data = path.read_bytes()
    if not 64 <= len(data) <= 65536 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("not a bounded little-endian ELF64 object")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if (e_type, e_machine) != (1, 62):
        raise ValueError("expected x86-64 ET_REL")
    offset = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum, shstr = struct.unpack_from("<HHH", data, 58)
    if shentsize != 64 or not 2 <= shnum <= 64 or shstr >= shnum:
        raise ValueError("invalid section header table")
    checked(data, offset, shnum * shentsize)
    headers = [
        struct.unpack_from("<IIQQQQIIQQ", data, offset + 64 * i)
        for i in range(shnum)
    ]
    shstr_section = headers[shstr]
    shstrings = checked(data, shstr_section[4], shstr_section[5])

    def name(table: bytes, index: int) -> bytes:
        if index >= len(table):
            raise ValueError("invalid ELF string offset")
        end = table.find(b"\0", index)
        if end == -1:
            raise ValueError("unterminated ELF string")
        return table[index:end]

    sections = [name(shstrings, h[0]) for h in headers]
    if sections.count(b".text") != 1:
        raise ValueError("expected exactly one .text section")
    text_index = sections.index(b".text")
    text_hdr = headers[text_index]
    if text_hdr[1] != 1 or not text_hdr[2] & 0x4:
        raise ValueError(".text not executable PROGBITS")
    code = checked(data, text_hdr[4], text_hdr[5])
    if any(h[1] in (4, 9) and h[7] == text_index and h[5] for h in headers):
        raise ValueError("text relocations cannot be copied as final instructions")
    symbol_tables = [h for h in headers if h[1] == 2]
    if len(symbol_tables) != 1:
        raise ValueError("expected one symbol table")
    symtab = symbol_tables[0]
    if symtab[9] != 24 or symtab[5] % 24 or symtab[6] >= len(headers):
        raise ValueError("invalid symbol table")
    strings_hdr = headers[symtab[6]]
    if strings_hdr[1] != 3:
        raise ValueError("invalid symbol strings")
    strings = checked(data, strings_hdr[4], strings_hdr[5])
    found = []
    for i in range(symtab[5] // 24):
        symbol = struct.unpack(
            "<IBBHQQ", checked(data, symtab[4] + i * 24, 24))
        if name(strings, symbol[0]) == symbol_name.encode("ascii"):
            found.append(symbol)
    if (len(found) != 1 or found[0][3] != text_index
            or found[0][4] != 0 or found[0][5] != len(code)):
        raise ValueError("missing or invalid bounded entry symbol")
    return code


def produce(client: bytes, provider: bytes) -> str:
    # The known instruction shape permits exactly the two guest-address
    # patches performed by the existing audited fixture.
    if client != bytes.fromhex("48bf2a00000000000000ff15000000000f0b"):
        raise ValueError("compiled client code diverged from the tested contract")
    if provider != bytes.fromhex("48b80000000000000000ffe0"):
        raise ValueError("compiled provider code diverged from the tested contract")
    arrays = []
    for name, code in (
        ("kCompiledOwnedClient", client),
        ("kCompiledOwnedProvider", provider),
    ):
        elems = ", ".join(f"std::byte{{0x{item:02x}}}" for item in code)
        arrays.append(
            f"inline constexpr std::array<std::byte, {len(code)}> "
            f"{name}{{{elems}}};"
        )
    return (
        "#pragma once\n#include <array>\n#include <cstddef>\n\n"
        + "\n".join(arrays) + "\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--provider", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    header = produce(
        decode_object(args.client, "astraea_owned_client_entry"),
        decode_object(args.provider, "astraea_owned_provider_entry"),
    )
    args.output.write_text(header, encoding="ascii")
    print("COMPILED OWNED X86-64 .text VERIFIED; NO RETAIL ENTRY CLAIM")


if __name__ == "__main__":
    main()
