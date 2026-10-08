# Public native-linker packed SCE metadata profile (experimental)

**Status:** first source-attributed read-only decoder; not PS5 retail ABI truth.

## Scoped source

The independently authored GPL-3.0 licensed
[Rufidj/ps5link-sdk](https://github.com/Rufidj/ps5link-sdk/tree/ea771e535378740b6a058b8e5419eb8a0e0e0ec8)
`linker/dynwriter.c` (around 2026-10-08, lines 462–490 and 849–853)
publicly emits its needed-module and import-library dynamic words as:

```text
bits  0..31: offset of published module/library name in .dynstr
bits 32..47: module/library version (16-bit)
bits 48..63: locally assigned module/library ID (16-bit)
```

The source specifically distinguishes a published module name from the
generic `DT_NEEDED` SONAME. Module local IDs and library local IDs use
**separate identity namespaces** and may legitimately overlap. A local ID
of zero cannot universally be declared invalid. This documented emitter
layout is **not** direct proof that every retail PS5 executable uses it.
No code or tables from the external linker were copied.

## CLI and safety boundary

The default `astraea dependencies <artifact>` manifest continues to
retain **opaque raw** SCE dynamic tag/value/source indices without decoding.

The additional explicit research-only mode is:

```text
astraea dependencies --public-sce-pack-v1 <lawful-public-eboot.elf>
```

Only in that mode, the manifest adds:

- `experimental_public_sce_pack_v1=1`;
- for each SCE needed-module/import-library record, exact
  `public_local_id`, `public_version`, `public_name_offset`, and
  lossless `public_name_hex` (max 256 bytes, validated via existing
  `DynamicStringTableDescriptor` and initialized image mappings);
- separate counts of distinct local IDs in the module and library
  categories, and a fail-closed conflict when one local ID appears
  with different version/name within its own category.

Every raw field and source entry index remains visible. There is no
conversion of these local IDs to opaque long-form symbol spelling,
automatic provider selection, HLE implementation, guest relocation
application, or retail native-entry authorization. The existing
`guest_instructions=0`, `resolution=not_attempted`, and
`relocation_application=not_attempted` remain mandatory.

Malformed strings, missing string tables, contradictory local ID data,
or invalid/too-long published names cause a nonzero exit without
a partial successful manifest. The CLI does not print untrusted
guest strings as raw terminal text.

## Reproducibility / next falsification

Pure field-split Catch2 tests cover independently authored extreme
bit patterns. The separate pinned BlackBear native-title integration
build (exact upstream
`2f672d1c2f508e26f82ce6e27cef289a0861413c`) checks that four
needed-module and four imported-library entries decode to distinct
local IDs with source-compatible versions and readable published names.
It also tests malformed input refusal.

This is a **toolchain-pattern concordance check**, not a commercial-title
fidelity claim: BlackBear's binary is independently built using a
different pinned source toolchain that yields compatible-looking
metadata. Before generalizing, independently test malformed dynamic
string offsets and ID conflicts using synthetic fully validated ELF
fixtures, and compare multiple independently authored/known-good
title producers. Only then consider a narrowly typed mapping
from `nid#library#module` import IDs to provider identities.

Related: #356, #367, #334. Sony firmware, proprietary binaries,
hardware jailbreaks and commercial executables are absent.
