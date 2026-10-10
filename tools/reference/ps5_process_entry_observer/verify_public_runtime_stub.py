#!/usr/bin/env python3
"""Inspect the pinned public PRX's source-authored _init_env export.

This is a bounded negative compatibility test, not a generic ELF loader.
Offsets and the expected code are verified against the *pinned* independent
BlackBear runtime builder (GPL-3.0-or-later; revision
2f672d1c2f508e26f82ce6e27cef289a0861413c). The test never loads or
executes the PRX and never establishes a Sony runtime contract.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

EXPECTED_PRX_SHA256 = "8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb"
EXPECTED_PRX_BYTES = 0x14629A
MAX_PRX_BYTES = 2_000_000
MAX_API_BYTES = 200_000
EXPORT_COUNT = 2566
METADATA_FILE_OFFSET = 0x11B810
SYMBOL_TABLE_OFFSET = 0xA7A8
STRING_TABLE_SIZE = 0xA7A3
TEXT_FILE_OFFSET = 0x4000
TARGET_NID = "bzQExy189ZI"
TARGET_API_ROW = TARGET_NID + "|1|2|0x1"
TARGET_EXPORT_SUFFIX = "#D#A"
GENERIC_RETURN_CODE = bytes.fromhex("660fefc031c0c3")


class EvidenceError(ValueError):
    pass


def bounded(blob: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(blob) or size > len(blob) - offset:
        raise EvidenceError("field outside bounded source artifact")
    return blob[offset:offset + size]


def api_index(raw: str, expected_count: int) -> int:
    entries = []
    for line in raw.splitlines():
        row = line.strip()
        if not row or row.startswith("#"):
            continue
        if len(row) > 128 or row.count("|") != 3:
            raise EvidenceError("malformed public API-surface entry")
        entries.append(row)
    if len(entries) != expected_count:
        raise EvidenceError("public API-surface count changed")
    if entries.count(TARGET_API_ROW) != 1:
        raise EvidenceError("missing or ambiguous public _init_env API row")
    if sum(row.split("|", 1)[0] == TARGET_NID for row in entries) != 1:
        raise EvidenceError("ambiguous exported NID")
    return entries.index(TARGET_API_ROW)


def inspect(prx: bytes, api: str, *, enforce_pinned: bool = True,
            export_count: int = EXPORT_COUNT) -> dict:
    if not 64 <= len(prx) <= MAX_PRX_BYTES or prx[:6] != b"\x7fELF\x02\x01":
        raise EvidenceError("expected bounded little-endian ELF64 PRX")
    if struct.unpack_from("<H", prx, 16)[0] != 0xFE18:
        raise EvidenceError("not the expected PS5-format dynamic module type")
    if struct.unpack_from("<H", prx, 18)[0] != 62:
        raise EvidenceError("not x86-64")
    if enforce_pinned and (
        len(prx) != EXPECTED_PRX_BYTES
        or hashlib.sha256(prx).hexdigest() != EXPECTED_PRX_SHA256
    ):
        raise EvidenceError("pinned generated PRX identity changed")

    index = api_index(api, export_count)
    at = METADATA_FILE_OFFSET + SYMBOL_TABLE_OFFSET + (index + 1) * 24
    name_offset, info, other, section, address, size = struct.unpack(
        "<IBBHQQ", bounded(prx, at, 24))
    if (info, other, section, address, size) != (0x12, 0, 3, 0x50, 1):
        raise EvidenceError("public export record no longer names shared return code")

    string = (TARGET_NID + TARGET_EXPORT_SUFFIX).encode("ascii") + b"\0"
    if name_offset >= STRING_TABLE_SIZE or len(string) > STRING_TABLE_SIZE - name_offset:
        raise EvidenceError("public export string offset is invalid")
    if bounded(prx, METADATA_FILE_OFFSET + name_offset, len(string)) != string:
        raise EvidenceError("public PRX export name differs from pinned API row")

    code = bounded(prx, TEXT_FILE_OFFSET + address, len(GENERIC_RETURN_CODE))
    if code != GENERIC_RETURN_CODE:
        raise EvidenceError("public PRX export is no longer the source-authored return stub")
    return {
        "classification": "source_authored_shared_return_stub",
        "source": "pinned_non_sony_prx_only",
        "export_nid": TARGET_NID,
        "export_dynamic_symbol_index": index + 1,
        "export_address": hex(address),
        "code_hex": code.hex(),
        "provider_satisfies_startup_initializer": False,
        "guest_execution": "none",
        "sony_abi": "not_established",
        "artifact_sha256": hashlib.sha256(prx).hexdigest(),
    }


def self_test() -> None:
    data = bytearray(METADATA_FILE_OFFSET + SYMBOL_TABLE_OFFSET + 48)
    data[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", data, 16, 0xFE18, 62)
    offset = METADATA_FILE_OFFSET + SYMBOL_TABLE_OFFSET + 24
    name = (TARGET_NID + TARGET_EXPORT_SUFFIX).encode() + b"\0"
    name_offset = 32
    data[METADATA_FILE_OFFSET + name_offset:METADATA_FILE_OFFSET + name_offset + len(name)] = name
    struct.pack_into("<IBBHQQ", data, offset, name_offset, 0x12, 0, 3, 0x50, 1)
    data[TEXT_FILE_OFFSET + 0x50:TEXT_FILE_OFFSET + 0x50 + 7] = GENERIC_RETURN_CODE
    api = "# test-only public interface\n" + TARGET_API_ROW + "\n"
    result = inspect(bytes(data), api, enforce_pinned=False, export_count=1)
    assert result["provider_satisfies_startup_initializer"] is False
    assert result["export_dynamic_symbol_index"] == 1

    for changed_api in ("", TARGET_API_ROW + "\n" + TARGET_API_ROW + "\n",
                        "OTHER123456|1|2|0x1\n"):
        try:
            inspect(bytes(data), changed_api, enforce_pinned=False, export_count=1)
        except EvidenceError:
            pass
        else:
            raise AssertionError("malformed public API manifest accepted")
    for position, replacement in (
        (TEXT_FILE_OFFSET + 0x50, 0x90),
        (offset + 8, 0x51),  # export value low byte
        (offset + 4, 0x11),  # ELF binding/type
        (METADATA_FILE_OFFSET + name_offset, ord("x")),
        (16, 2),  # invalid dynamic module type
    ):
        damaged = bytearray(data)
        damaged[position] = replacement
        try:
            inspect(bytes(damaged), api, enforce_pinned=False, export_count=1)
        except EvidenceError:
            pass
        else:
            raise AssertionError("modified source artifact was admitted")
    try:
        inspect(bytes(data), api, enforce_pinned=True, export_count=1)
    except EvidenceError:
        pass
    else:
        raise AssertionError("unhashed synthetic input passed pinned policy")
    print("PINNED PUBLIC RUNTIME STUB REFUSAL SELF-TEST PASS; NO EXECUTION")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--prx", type=Path)
    parser.add_argument("--api-surface", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.prx is None or args.api_surface is None:
        parser.error("--prx and --api-surface are required")
    if not 0 < args.prx.stat().st_size <= MAX_PRX_BYTES:
        raise EvidenceError("PRX too large or empty")
    if not 0 < args.api_surface.stat().st_size <= MAX_API_BYTES:
        raise EvidenceError("API manifest too large or empty")
    result = inspect(
        args.prx.read_bytes(), args.api_surface.read_text(encoding="ascii"))
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (EvidenceError, OSError, UnicodeError, ValueError, struct.error) as error:
        print("public_runtime_stub_error=" + str(error), file=sys.stderr)
        sys.exit(1)
