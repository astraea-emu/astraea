#!/usr/bin/env python3
"""Read-only cross-check of pinned independent Breakout ELF import provenance.

The public author's link-time IMPORTS dictionary assigns NIDs and libraries.
Astraea's manifest independently reports opaque names and relocations.
Equality proves source-to-artifact identity, never that a Sony ABI or runtime
provider has been implemented. Nothing is loaded or executed in this script.
"""
import argparse
import ast
import hashlib
import re
import struct
from collections import Counter
from pathlib import Path

PINNED_AUTHOR_SOURCE_BLOB_SHA1 = "907f2de5d3e98ec68cc2f29495190ebed3c304b6"
PINNED_BREAKOUT_ELF_SHA256 = (
    "05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8"
)
MAX_ELF_BYTES = 16 * 1024 * 1024
MAX_U64 = (1 << 64) - 1
PINNED_MODULE_COUNTS = {
    "libkernel": 6,
    "libSceVideoOut": 6,
    "libSceSystemService": 1,
    "libSceUserService": 2,
    "libScePad": 4,
}
MAX_SOURCE_BYTES = 128 * 1024
MAX_MANIFEST_BYTES = 512 * 1024
NID_PATTERN = re.compile(r"[A-Za-z0-9+/\-=]{11}\Z")
FIELD_PATTERN = re.compile(
    r"(symbol|generic_needed|relocation)\[(\d+)\]\."
    r"(name_hex|undefined|type|symbol_index|target)=(.*)\Z"
)


class AuditFailure(ValueError):
    pass


def bounded_bytes(path: Path, limit: int) -> bytes:
    with path.open("rb") as file:
        data = file.read(limit + 1)
    if len(data) > limit:
        raise AuditFailure(f"input_too_large:{path.name}")
    return data


def parse_author_imports(source: str) -> dict[str, tuple[str, str]]:
    tree = ast.parse(source)
    assignments = [
        node.value for node in tree.body
        if isinstance(node, ast.Assign)
        and len(node.targets) == 1
        and isinstance(node.targets[0], ast.Name)
        and node.targets[0].id == "IMPORTS"
    ]
    if len(assignments) != 1:
        raise AuditFailure("missing_or_duplicate_author_imports")
    catalog = ast.literal_eval(assignments[0])
    if not isinstance(catalog, dict):
        raise AuditFailure("author_imports_not_a_mapping")
    if {k: len(v) for k, v in catalog.items()
            if isinstance(v, dict)} != PINNED_MODULE_COUNTS:
        raise AuditFailure("author_module_catalog_drift")
    if set(catalog) != set(PINNED_MODULE_COUNTS):
        raise AuditFailure("author_module_set_drift")

    by_nid: dict[str, tuple[str, str]] = {}
    for module, functions in catalog.items():
        for function, nid in functions.items():
            if (not isinstance(function, str) or
                    not function.startswith("sce") or
                    not isinstance(nid, str) or
                    NID_PATTERN.fullmatch(nid) is None or
                    nid in by_nid):
                raise AuditFailure("invalid_or_duplicate_author_nid")
            by_nid[nid] = (module, function)
    if len(by_nid) != 19:
        raise AuditFailure("unexpected_author_import_count")
    return by_nid


def manifest_records(text: str) -> dict[str, dict[int, dict[str, str]]]:
    result: dict[str, dict[int, dict[str, str]]] = {
        "symbol": {}, "generic_needed": {}, "relocation": {}
    }
    for line in text.splitlines():
        matched = FIELD_PATTERN.fullmatch(line)
        if matched is None:
            continue
        group, idx_text, field, value = matched.groups()
        index = int(idx_text)
        if index >= 4096:
            raise AuditFailure("manifest_record_index_unbounded")
        record = result[group].setdefault(index, {})
        if field in record:
            raise AuditFailure(f"duplicate_manifest_field:{group}[{index}].{field}")
        record[field] = value
    return result


def decode_hex(value: str) -> str:
    if len(value) > 512 or len(value) % 2 != 0:
        raise AuditFailure("invalid_hex_length")
    try:
        return bytes.fromhex(value).decode("ascii")
    except (ValueError, UnicodeDecodeError) as exc:
        raise AuditFailure("invalid_manifest_hex") from exc


def verify_manifest(
    authored: dict[str, tuple[str, str]], text: str
) -> dict[str, int]:
    records = manifest_records(text)
    symbols = records["symbol"]
    needed = records["generic_needed"]
    relocations = records["relocation"]

    if set(symbols) != set(range(20)) or set(needed) != set(range(5)):
        raise AuditFailure("unexpected_symbol_or_dependency_indices")
    if set(relocations) != set(range(20)):
        raise AuditFailure("unexpected_relocation_indices")
    if decode_hex(symbols[0].get("name_hex", "X")) != "" or (
        symbols[0].get("undefined") != "1" or
        symbols[0].get("type") != "0"
    ):
        raise AuditFailure("invalid_reserved_elf_symbol_zero")

    observed: list[str] = []
    for index in range(1, 20):
        symbol = symbols[index]
        if symbol.get("undefined") != "1" or symbol.get("type") != "2":
            raise AuditFailure("non_external_function_symbol")
        observed.append(decode_hex(symbol.get("name_hex", "X")))
    if len(set(observed)) != 19 or set(observed) != set(authored):
        raise AuditFailure("external_nid_catalog_mismatch")

    libs = [
        decode_hex(needed[i].get("name_hex", "X"))
        for i in range(5)
    ]
    if len(set(libs)) != 5 or set(libs) != {
        f"{module}.sprx" for module in PINNED_MODULE_COUNTS
    }:
        raise AuditFailure("needed_library_catalog_mismatch")

    referenced: list[tuple[int, int]] = []
    for index in range(20):
        record = relocations[index]
        try:
            kind = int(record["type"], 10)
            symbol_index = int(record["symbol_index"], 10)
        except (KeyError, ValueError) as exc:
            raise AuditFailure("invalid_relocation_record") from exc
        referenced.append((kind, symbol_index))
    expected = [(8, 0)] + [(6, i) for i in range(1, 20)]
    if sorted(referenced) != sorted(expected):
        raise AuditFailure("relocation_demand_drift")

    # Never infer module binding from the ELF's bare NID. These labels
    # originate exclusively in the frozen independent author's source.
    modules = Counter(module for module, _function in authored.values())
    if dict(modules) != PINNED_MODULE_COUNTS:
        raise AuditFailure("module_declaration_count_mismatch")
    return dict(modules)


def checked_loads(elf: bytes) -> list[tuple[int, int, int]]:
    """Inspect file-backed PT_LOAD bounds without loading or executing it."""
    if hashlib.sha256(elf).hexdigest() != PINNED_BREAKOUT_ELF_SHA256:
        raise AuditFailure("pinned_elf_identity_drift")
    if len(elf) < 64 or elf[:7] != b"\x7fELF\x02\x01\x01":
        raise AuditFailure("invalid_elf64_identity")
    if (elf[7], elf[8]) != (9, 2):
        raise AuditFailure("invalid_ps5_elf_abi")
    file_type, machine = struct.unpack_from("<HH", elf, 16)
    if file_type not in (2, 3, 0xFE10, 0xFE18) or machine != 62:
        raise AuditFailure("invalid_ps5_elf_machine_or_type")
    phoff = struct.unpack_from("<Q", elf, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 54)
    if phentsize != 56 or phnum != 4 or phoff > len(elf) or (
        phnum * phentsize > len(elf) - phoff
    ):
        raise AuditFailure("invalid_program_header_bounds")
    loads: list[tuple[int, int, int]] = []
    for index in range(phnum):
        kind, flags, fileoff, vaddr, _paddr, filesz, memsz, _align = (
            struct.unpack_from("<IIQQQQQQ", elf, phoff + index * phentsize)
        )
        if kind != 1:
            continue
        if filesz > memsz or fileoff > len(elf) or filesz > len(elf) - fileoff:
            raise AuditFailure("invalid_load_file_extent")
        if not memsz or vaddr > MAX_U64 - memsz:
            raise AuditFailure("invalid_load_memory_extent")
        loads.append((vaddr, vaddr + memsz, flags))
    if len(loads) != 3:
        raise AuditFailure("unexpected_load_segment_count")
    return loads


def verify_relocation_write_extents(
    manifest: str, loads: list[tuple[int, int, int]]
) -> tuple[int, int]:
    """Require all 20 word64 writes to be uniquely mapped and writable."""
    relocations = manifest_records(manifest)["relocation"]
    if set(relocations) != set(range(20)):
        raise AuditFailure("unexpected_relocation_indices")
    ranges: list[tuple[int, int]] = []
    glob_dat = relative = 0
    for index in range(20):
        row = relocations[index]
        value = row.get("target", "")
        if re.fullmatch(r"0x[0-9a-fA-F]{1,16}", value) is None:
            raise AuditFailure("invalid_relocation_target_encoding")
        address = int(value, 16)
        if address > MAX_U64 - 8:
            raise AuditFailure("relocation_write_overflow")
        end = address + 8
        containing = [
            (low, high, flags) for low, high, flags in loads
            if low <= address and end <= high
        ]
        if len(containing) != 1:
            raise AuditFailure("relocation_target_not_uniquely_mapped")
        flags = containing[0][2]
        if not (flags & 2):
            raise AuditFailure("relocation_target_not_writable")
        if flags & 1:
            raise AuditFailure("relocation_target_executable")
        if any(address < old_end and old_start < end
               for old_start, old_end in ranges):
            raise AuditFailure("relocation_write_overlap")
        ranges.append((address, end))
        if row.get("type") == "8" and row.get("symbol_index") == "0":
            relative += 1
        elif row.get("type") == "6":
            glob_dat += 1
        else:
            raise AuditFailure("unclassified_relocation_write")
    if (relative, glob_dat) != (1, 19):
        raise AuditFailure("relocation_type_or_count_drift")
    return relative, glob_dat


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--author-source", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    args = parser.parse_args()
    try:
        source = bounded_bytes(args.author_source, MAX_SOURCE_BYTES)
        manifest = bounded_bytes(args.manifest, MAX_MANIFEST_BYTES)
        elf = bounded_bytes(args.elf, MAX_ELF_BYTES)
        prefix = b"blob " + str(len(source)).encode("ascii") + b"\0"
        source_blob_sha = hashlib.sha1(prefix + source).hexdigest()
        if source_blob_sha != PINNED_AUTHOR_SOURCE_BLOB_SHA1:
            raise AuditFailure("author_source_blob_identity_drift")
        authored = parse_author_imports(source.decode("utf-8"))
        manifest_text = manifest.decode("utf-8")
        counts = verify_manifest(authored, manifest_text)
        relative_count, glob_dat_count = verify_relocation_write_extents(
            manifest_text, checked_loads(elf)
        )
    except (AuditFailure, UnicodeDecodeError, SyntaxError, OSError) as exc:
        parser.exit(2, f"BREAKOUT AUTHOR IMPORT AUDIT REFUSED: {exc}\n")
    print("BREAKOUT AUTHOR IMPORT AUDIT PASS")
    print(f"author_build_py_blob_sha1={PINNED_AUTHOR_SOURCE_BLOB_SHA1}")
    print("external_import_nids=19")
    print("source_to_elf_nid_matches=19")
    print("unmatched_or_duplicate_external_nids=0")
    print("relocations=1_relative+19_glob_dat")
    print("needed_libraries=5")
    print(f"pinned_elf_sha256={PINNED_BREAKOUT_ELF_SHA256}")
    print(f"mapped_writable_relative_targets={relative_count}")
    print(f"mapped_writable_glob_dat_targets={glob_dat_count}")
    print("relocation_target_overlap=none")
    print("relocation_executable_targets=none")
    for module in sorted(counts):
        print(f"author_declared_{module}={counts[module]}")
    print("sony_export_resolution=not_attempted")
    print("import_application=not_attempted")
    print("guest_instructions=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
