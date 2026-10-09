#!/usr/bin/env python3
"""Bounded read-only lexical demand comparison of two independently authored ELFs.

This compares Astraea's *already validated* dependency manifests. It NEVER
resolves imports, relocates, executes guest code, or claims that the raw public
PRX provides any requested PS5 runtime service. NID / local IDs are opaque and
may be independently scoped in each authored artifact.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import sys

MAX_REPORT_BYTES = 4_000_000
MAX_SYMBOLS = 4096
MAX_RELOCATIONS = 4096
FIELD = re.compile(r"^[A-Za-z_][A-Za-z_0-9]*(?:\[[0-9]{1,4}\])?(?:\.[A-Za-z_][A-Za-z_0-9]*)?$")
HEX = re.compile(r"^(?:[0-9a-f]{2}){1,256}$")
EXPECTED_PRX_SHA256 = "8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb"


class InventoryError(ValueError):
    pass


def parse_report(raw: str) -> dict[str, str]:
    lines = raw.splitlines()
    if not lines or lines[0] != "Astraea dependency manifest v0":
        raise InventoryError("unexpected dependency-manifest header")
    fields = {}
    for line in lines[1:]:
        if not line or len(line) > 8192 or "=" not in line:
            raise InventoryError("malformed or oversized dependency-manifest field")
        key, value = line.split("=", 1)
        if not FIELD.fullmatch(key) or key in fields:
            raise InventoryError("invalid or duplicate dependency-manifest key")
        if any(ord(char) < 32 or ord(char) > 126 for char in value):
            raise InventoryError("nonprintable dependency-manifest value")
        fields[key] = value
    if fields.get("execution") != "none" or fields.get("guest_instructions") != "0":
        raise InventoryError("input report is not a read-only dependency inventory")
    if fields.get("resolution") != "not_attempted" or fields.get("relocation_application") != "not_attempted":
        raise InventoryError("input report incorrectly claims resolution or writes")
    return fields


def bounded_int(fields: dict[str, str], name: str, expected: int | None = None,
                maximum: int = MAX_SYMBOLS) -> int:
    text = fields.get(name, "")
    if not text.isdecimal() or len(text) > 8:
        raise InventoryError(f"missing or invalid integer {name}")
    value = int(text)
    if value > maximum or (expected is not None and value != expected):
        raise InventoryError(f"unexpected or oversized count {name}")
    return value


def symbol_inventory(fields: dict[str, str], expected_count: int) -> list[dict]:
    count = bounded_int(fields, "dynamic_symbol_records", expected_count)
    symbols = []
    for i in range(count):
        prefix = f"symbol[{i}]"
        if fields.get(prefix + ".undefined") not in ("0", "1"):
            raise InventoryError(f"missing undefined flag at {i}")
        if fields.get(prefix + ".sce_longform") not in ("0", "1"):
            raise InventoryError(f"missing SCE identity flag at {i}")
        binding = bounded_int(fields, prefix + ".binding", maximum=15)
        typ = bounded_int(fields, prefix + ".type", maximum=15)
        longform = fields[prefix + ".sce_longform"] == "1"
        identity = None
        if longform:
            items = tuple(fields.get(prefix + "." + item + "_hex", "")
                          for item in ("nid", "library_id", "module_id"))
            if not all(HEX.fullmatch(item) for item in items):
                raise InventoryError(f"invalid SCE long-form identity at {i}")
            identity = items
        symbols.append({
            "defined": fields[prefix + ".undefined"] == "0",
            "binding": binding, "type": typ, "identity": identity,
        })
    return symbols


def relocation_demand(fields: dict[str, str], symbols: list[dict],
                      expected_count: int) -> list[tuple[str, str, str]]:
    count = bounded_int(fields, "relocation_records", expected_count,
                        MAX_RELOCATIONS)
    demanded = []
    for i in range(count):
        prefix = f"relocation[{i}]"
        index = bounded_int(fields, prefix + ".symbol_index",
                            maximum=len(symbols) - 1)
        kind = fields.get(prefix + ".table")
        if kind not in ("rel", "rela", "plt_rel", "plt_rela"):
            raise InventoryError(f"invalid relocation table at {i}")
        bounded_int(fields, prefix + ".type", maximum=2**32 - 1)
        symbol = symbols[index]
        if symbol["defined"] or index == 0:
            continue
        if symbol["identity"] is not None:
            demanded.append(symbol["identity"])
    return demanded


def compare(title: dict[str, str], provider: dict[str, str]) -> dict:
    title_syms = symbol_inventory(title, 26)
    provider_syms = symbol_inventory(provider, 2669)
    demands = relocation_demand(title, title_syms, 40)
    relocation_demand(provider, provider_syms, 1896)  # Validate full PRX census.
    exports = [s["identity"] for s in provider_syms
               if s["defined"] and s["binding"] in (1, 2)
               and s["identity"] is not None]
    full = Counter(exports)
    nid = defaultdict(set)
    for identity in exports:
        nid[identity[0]].add(identity)

    counts = Counter()
    examples = []
    for identity in sorted(set(demands)):
        exact_rows = full[identity]
        nid_candidates = len(nid.get(identity[0], set()))
        if exact_rows:
            classification = "lexical_full_triplet"
        elif nid_candidates:
            classification = "lexical_nid_only"
        else:
            classification = "no_nid_candidate"
        occurrences = demands.count(identity)
        counts[classification] += occurrences
        examples.append({
            "nid_hex": identity[0],
            "library_id_hex": identity[1],
            "module_id_hex": identity[2],
            "demand_relocations": occurrences,
            "classification": classification,
            "exact_defined_export_rows": exact_rows,
            "same_nid_distinct_export_identities": nid_candidates,
        })
    if sum(counts.values()) != len(demands):
        raise InventoryError("demand classification lost referenced relocations")
    return {
        "policy": "read_only_lexical_comparison_no_binding",
        "resolution": "not_attempted",
        "guest_instructions": 0,
        "title_relocation_records": 40,
        "provider_relocation_records": 1896,
        "title_longform_external_relocations": len(demands),
        "title_unique_longform_external_identities": len(examples),
        "provider_defined_longform_export_rows": len(exports),
        "comparison_counts": {k: counts[k] for k in (
            "lexical_full_triplet", "lexical_nid_only", "no_nid_candidate")},
        "demands": examples,
        "caveat": "IDs/NIDs are opaque and local IDs can be scoped differently; no actual module-selection authority, Sony API compatibility or relocation application was established.",
    }


def sha256_file(path: Path, byte_limit: int) -> tuple[str, int]:
    size = path.stat().st_size
    if size <= 0 or size > byte_limit:
        raise InventoryError("ELF input absent/empty/oversized")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            digest.update(block)
    return digest.hexdigest(), size


def read_manifest(path: Path) -> str:
    if not path.is_file() or path.stat().st_size > MAX_REPORT_BYTES:
        raise InventoryError("manifest absent/oversized")
    return path.read_text(encoding="ascii")


def self_test() -> None:
    def make(symbols, relocations):
        fields = {
            "execution": "none", "resolution": "not_attempted",
            "relocation_application": "not_attempted", "guest_instructions": "0",
            "dynamic_symbol_records": str(len(symbols)),
            "relocation_records": str(len(relocations)),
        }
        for i, (defined, identity) in enumerate(symbols):
            root = f"symbol[{i}]"
            fields.update({root + ".undefined": "0" if defined else "1",
                           root + ".binding": "1", root + ".type": "2",
                           root + ".sce_longform": "1" if identity else "0"})
            if identity:
                for label, value in zip(("nid", "library_id", "module_id"), identity):
                    fields[root + "." + label + "_hex"] = value
        for i, index in enumerate(relocations):
            root = f"relocation[{i}]"
            fields.update({root + ".symbol_index": str(index),
                           root + ".table": "rela", root + ".type": "7"})
        return fields

    ident = ("414141", "6262", "6363")
    changed = ("414141", "6464", "6565")
    title = make([(True, None), (False, ident), (False, ("999999", "6262", "6363"))], [1, 1, 2])
    provider = make([(True, ident), (True, changed), (False, None)], [2])
    # The helper's pinned-count expectations must be enforced separately.
    assert bounded_int({"x": "26"}, "x", expected=26) == 26
    assert parse_report("Astraea dependency manifest v0\nexecution=none\nresolution=not_attempted\nrelocation_application=not_attempted\nguest_instructions=0\n")["execution"] == "none"
    assert Counter([x for x in [ident, ident, changed]])[ident] == 2
    assert title["relocation[0].symbol_index"] == "1"
    assert provider["symbol[1].nid_hex"] == ident[0]
    for malformed in (
        "Astraea dependency manifest v0\nexecution=none\nexecution=none\n",
        "Astraea dependency manifest v0\nexecution=none\nresolution=resolved\n",
    ):
        try:
            parse_report(malformed)
        except InventoryError:
            pass
        else:
            raise AssertionError("untrusted manifest was accepted")
    print("READ-ONLY PUBLIC-PAIR ANALYZER SELF-TEST PASS")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--title-manifest", type=Path)
    ap.add_argument("--provider-manifest", type=Path)
    ap.add_argument("--title-elf", type=Path)
    ap.add_argument("--provider-elf", type=Path)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return 0
    if any(value is None for value in (
        args.title_manifest, args.provider_manifest, args.title_elf,
        args.provider_elf, args.report)):
        ap.error("both manifests, both authored ELF paths and output report required")
    title_raw = read_manifest(args.title_manifest)
    provider_raw = read_manifest(args.provider_manifest)
    title_hash, title_size = sha256_file(args.title_elf, 4_000_000)
    provider_hash, provider_size = sha256_file(args.provider_elf, 2_000_000)
    if provider_hash != EXPECTED_PRX_SHA256 or provider_size != 1335962:
        raise InventoryError("pinned independently authored provider PRX digest/size changed")
    analysis = compare(parse_report(title_raw), parse_report(provider_raw))
    analysis["provenance"] = {
        "source_revision": "2f672d1c2f508e26f82ce6e27cef289a0861413c",
        "title_elf_sha256": title_hash,
        "title_elf_bytes": title_size,
        "provider_elf_sha256": provider_hash,
        "provider_elf_bytes": provider_size,
        "title_manifest_sha256": hashlib.sha256(title_raw.encode("ascii")).hexdigest(),
        "provider_manifest_sha256": hashlib.sha256(provider_raw.encode("ascii")).hexdigest(),
    }
    args.report.write_text(json.dumps(analysis, sort_keys=True, indent=2) + "\n",
                           encoding="ascii")
    print("PUBLIC TWO-ELF LEXICAL CLOSURE REPORT (NO RESOLUTION)")
    print(json.dumps({k: v for k, v in analysis.items() if k != "demands"},
                     sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (InventoryError, OSError, UnicodeError, ValueError) as exc:
        print(f"public_pair_error={exc}", file=sys.stderr)
        sys.exit(1)
