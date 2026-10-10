#!/usr/bin/env python3
"""Read-only identity check between pinned authored intermediate and SCE ELF.

This validates one converter-specific transformation of an independently
created source symbol. It never resolves a runtime provider or runs guest code.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct
import sys

# Independently authored pinned BlackBear SCE converter; not a Sony ABI claim.
PUBLIC_CONVERTER_SUFFIX = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")
MAX_ELF_BYTES = 2_000_000
MAX_MANIFEST_BYTES = 4_000_000
MAX_SYMBOLS = 4096
MAX_RELOCATIONS = 4096


class VerificationError(ValueError):
    pass


def check_range(blob: bytes, offset: int, size: int) -> None:
    if offset < 0 or size < 0 or offset > len(blob) or size > len(blob) - offset:
        raise VerificationError("ELF section or symbol outside file")


def read_symbol_index(data: bytes, requested: bytes) -> tuple[int, int]:
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise VerificationError("expected ELF64 little-endian input")
    if struct.unpack_from("<H", data, 18)[0] != 62:
        raise VerificationError("expected AMD64 ELF")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum = struct.unpack_from("<HH", data, 58)
    if shentsize != 64 or not 2 < shnum <= 4096:
        raise VerificationError("invalid ELF section header table")
    check_range(data, shoff, shnum * shentsize)
    sec = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + 64 * i)
           for i in range(shnum)]
    tables = [(i, s) for i, s in enumerate(sec) if s[1] == 11]
    if len(tables) != 1:
        raise VerificationError("expected one dynamic symbol table")
    _, s = tables[0]
    offset, size, link, entsize = s[4], s[5], s[6], s[9]
    if entsize != 24 or size % 24 or not 0 < link < len(sec) or sec[link][1] != 3:
        raise VerificationError("invalid dynamic symbol table geometry")
    count = size // 24
    if not 1 < count <= MAX_SYMBOLS:
        raise VerificationError("invalid dynamic symbol count")
    check_range(data, offset, size)
    strings_offset, strings_size = sec[link][4], sec[link][5]
    check_range(data, strings_offset, strings_size)
    strings = data[strings_offset:strings_offset + strings_size]
    found = []
    for i in range(count):
        name, _info, _other, section, _value, _sz = struct.unpack_from(
            "<IBBHQQ", data, offset + 24 * i)
        if name >= len(strings):
            raise VerificationError("symbol name outside string table")
        end = strings.find(b"\0", name)
        if end < 0:
            raise VerificationError("unterminated symbol name")
        if strings[name:end] == requested:
            if section != 0 or i == 0:
                raise VerificationError("requested intermediate symbol is not undefined")
            found.append(i)
    if len(found) != 1:
        raise VerificationError("missing or ambiguous intermediate undefined symbol")
    return found[0], count


def converter_nid(symbol: bytes) -> str:
    digest = hashlib.sha1(symbol + PUBLIC_CONVERTER_SUFFIX).digest()
    return base64.b64encode(digest[:8][::-1]).decode("ascii")[:11].replace("/", "-")


def parse_manifest(raw: str) -> dict[str, str]:
    if not raw.startswith("Astraea dependency manifest v0\n"):
        raise VerificationError("unexpected manifest header")
    fields = {}
    for line in raw.splitlines()[1:]:
        if not line or len(line) > 8192 or "=" not in line:
            raise VerificationError("invalid manifest field")
        key, value = line.split("=", 1)
        cleaned = key.replace("_", "").replace(".", "").replace("[", "").replace("]", "")
        if not key or key in fields or not key.isascii() or not cleaned.isalnum():
            raise VerificationError("duplicate or malformed manifest key")
        if not value.isascii() or any(ord(char) < 32 or ord(char) > 126 for char in value):
            raise VerificationError("unprintable manifest field")
        fields[key] = value
    for name, value in (
        ("execution", "none"),
        ("guest_instructions", "0"),
        ("resolution", "not_attempted"),
        ("relocation_application", "not_attempted"),
    ):
        if fields.get(name) != value:
            raise VerificationError("manifest violates read-only boundary")
    return fields


def number(fields: dict[str, str], key: str, maximum: int) -> int:
    text = fields.get(key, "")
    if not text.isdecimal() or len(text) > 8 or int(text) > maximum:
        raise VerificationError("missing or invalid manifest count " + key)
    return int(text)


def verify(intermediate: bytes, manifest: str, symbol: bytes,
           expected_symbols: int, expected_relocations: int) -> dict:
    index, count = read_symbol_index(intermediate, symbol)
    fields = parse_manifest(manifest)
    if count != expected_symbols or number(fields, "dynamic_symbol_records", MAX_SYMBOLS) != count:
        raise VerificationError("intermediate and final dynamic symbol count differ")
    rc = number(fields, "relocation_records", MAX_RELOCATIONS)
    if rc != expected_relocations:
        raise VerificationError("relocation-count baseline changed")
    key = f"symbol[{index}]"
    if fields.get(key + ".undefined") != "1" or fields.get(key + ".sce_longform") != "1":
        raise VerificationError("converted target is not an undefined SCE long-form import")
    nid = converter_nid(symbol)
    if fields.get(key + ".nid_hex") != nid.encode("ascii").hex():
        raise VerificationError("final import NID disagrees with pinned public converter")
    try:
        raw = bytes.fromhex(fields[key + ".name_hex"])
        library = bytes.fromhex(fields[key + ".library_id_hex"])
        module = bytes.fromhex(fields[key + ".module_id_hex"])
    except (KeyError, ValueError) as error:
        raise VerificationError("malformed final symbol identity") from error
    if not library or not module or b"#" in library or b"#" in module:
        raise VerificationError("malformed local library or module identity")
    if raw != nid.encode("ascii") + b"#" + library + b"#" + module:
        raise VerificationError("final SCE long-form identity mismatch")
    usage = sum(
        number(fields, f"relocation[{i}].symbol_index", count - 1) == index
        for i in range(rc)
    )
    if usage == 0:
        raise VerificationError("target import has no actual relocation demand")
    return dict(
        policy="pinned_public_converter_only",
        execution="none",
        guest_instructions=0,
        provider_authority="unverified",
        intermediate_dynamic_symbol_index=index,
        converter_nid=nid,
        local_library_id=library.decode("ascii"),
        local_module_id=module.decode("ascii"),
        referenced_relocations=usage,
    )


def self_test() -> None:
    # Independent minimal ELF64, undefined dynamic symbol at index 1.
    data = bytearray(1024)
    data[:16] = b"\x7fELF\x02\x01\x01" + b"\x00" * 9
    struct.pack_into("<HHI", data, 16, 3, 62, 1)
    struct.pack_into("<Q", data, 40, 512)
    struct.pack_into("<HHH", data, 58, 64, 3, 0)
    struct.pack_into("<IBBHQQ", data, 128 + 24, 1, 0x12, 0, 0, 0, 0)
    table = b"\0_init_env\0"
    data[256:256 + len(table)] = table
    struct.pack_into("<IIQQQQIIQQ", data, 512 + 64, 0, 11, 0, 0, 128, 48, 2, 0, 8, 24)
    struct.pack_into("<IIQQQQIIQQ", data, 512 + 128, 0, 3, 0, 0, 256, len(table), 0, 0, 1, 0)
    nid = converter_nid(b"_init_env")
    assert nid == "bzQExy189ZI"
    fields = {
        "execution": "none", "guest_instructions": "0",
        "resolution": "not_attempted", "relocation_application": "not_attempted",
        "dynamic_symbol_records": "2", "relocation_records": "1",
        "symbol[1].undefined": "1", "symbol[1].sce_longform": "1",
        "symbol[1].nid_hex": nid.encode().hex(),
        "symbol[1].name_hex": (nid + "#A#B").encode().hex(),
        "symbol[1].library_id_hex": "41", "symbol[1].module_id_hex": "42",
        "relocation[0].symbol_index": "1",
    }
    report = lambda values: "Astraea dependency manifest v0\n" + "".join(
        f"{key}={value}\n" for key, value in values.items())
    assert verify(bytes(data), report(fields), b"_init_env", 2, 1)["referenced_relocations"] == 1
    for change in (
        {"symbol[1].nid_hex": "00"},
        {"relocation[0].symbol_index": "0"},
        {"symbol[1].name_hex": "00"},
        {"execution": "guest"},
        {"symbol[1].undefined": "0"},
    ):
        altered = {**fields, **change}
        try:
            verify(bytes(data), report(altered), b"_init_env", 2, 1)
        except VerificationError:
            pass
        else:
            raise AssertionError("invalid manifest was accepted")
    try:
        verify(bytes(data), report(fields), b"nonexistent", 2, 1)
    except VerificationError:
        pass
    else:
        raise AssertionError("missing intermediate symbol was accepted")
    try:
        parse_manifest(report(fields) + "execution=none\n")
    except VerificationError:
        pass
    else:
        raise AssertionError("duplicate field was accepted")
    print("PINNED CONVERTER IDENTITY SELF-TEST PASS; NO GUEST EXECUTION")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--intermediate", type=Path)
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--symbol", default="_init_env")
    ap.add_argument("--expected-symbols", type=int, default=26)
    ap.add_argument("--expected-relocations", type=int, default=40)
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.intermediate or not args.manifest:
        ap.error("--intermediate and --manifest are required")
    if not 0 < args.intermediate.stat().st_size <= MAX_ELF_BYTES or not 0 < args.manifest.stat().st_size <= MAX_MANIFEST_BYTES:
        raise VerificationError("input exceeds bounded size or is empty")
    result = verify(
        args.intermediate.read_bytes(), args.manifest.read_text("ascii"),
        args.symbol.encode("ascii"), args.expected_symbols, args.expected_relocations,
    )
    result["intermediate_sha256"] = hashlib.sha256(args.intermediate.read_bytes()).hexdigest()
    result["manifest_sha256"] = hashlib.sha256(args.manifest.read_bytes()).hexdigest()
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (VerificationError, OSError, ValueError, UnicodeError) as error:
        print("converter_identity_error=" + str(error), file=sys.stderr)
        sys.exit(1)
