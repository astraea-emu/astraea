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

The first successfully tested independent public-title build
([run 37850423653](https://github.com/astraea-emu/astraea/actions/runs/37850423653))
produced these exact published names:

| Generic SONAME family | Packed needed-module local ID | Packed import-library local ID | Version |
| --- | ---: | ---: | ---: |
| `libSceLibcInternal` | 1 | 0 | 1 |
| `libSceSystemService` | 2 | 1 | 1 |
| `libSceVideoOut` | 3 | 2 | 1 |
| `libkernel` | 4 | 3 | 1 |

In this independently authored sample, the module and library published
name bytes happen to be identical for each row, but the generic
`DT_NEEDED` SONAME has a `.prx` suffix. That relationship is
**observed for this sample**, not universally stipulated. A refined
exact-byte pinned-title CI assertion protects these particular rows.

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

## Public native-symbol ID cross-check (stage J)

The two independently authored, reviewed public title linkers encode
numeric **local library/module IDs** into long-form import spellings using
the compact alphabet
`ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-`.
Examples are `A = 0`, `B = 1`, `BA = 64`, and `P-- = 65535`.
This is **not** the eleven-character NID codec.

The opt-in public profile decodes each structurally valid
`nid#library-id#module-id` suffix and requires both decoded numeric
IDs to appear in their *own* previously validated packed metadata
ledgers. It reports both local IDs and both published names, losslessly
hex encoded. Invalid characters, noncanonical leading-zero spellings,
out-of-range or unregistered local IDs cause an atomic diagnostic
failure. No symbol is associated with a service implementation, HLE
function or loadable Sony module; **matched means metadata agrees**.

Source-derived evidence:
- BlackBear `tooling/native/sce_module_writer.cpp` at
  `2f672d1c2f508e26f82ce6e27cef289a0861413c`;
- GPLv3 ps5link SDK `linker/nid.c` and `linker/linker.c` at
  `ea771e535378740b6a058b8e5419eb8a0e0e0ec8`.

A pinned independent BlackBear title check expects 25 structurally
long-form symbol identities to identify the four published module
names through distinct module IDs 1..4 and library IDs 0..3. The first
symbol row is reserved and does not acquire a module association.

The matching pairs here reflect this **exact public emitter** and
cannot establish the correct provider lookup, version-compatibility
policy, symbol-export availability, process bootstrap state or
commercial-title compatibility on real hardware. Generalizing this
relationship requires separate, authenticated observations.


## Public relocation-demand accounting (stage K)

For the explicitly selected `--public-sce-pack-v1` experiment only,
Astraea combines its **already validated** SCE import suffix ledger with
each previously validated relocation-to-symbol reference. It classifies
each entry as a reserved/null symbol, a locally defined symbol, an
undefined symbol without a classified public identity, or an undefined
public external import. Every relocation retains its original table/type,
addend, target, raw information and symbol index.

For the last category, the manifest additionally reports the exact
**public local module ID, local library ID and NID bytes** associated with
the referenced import. It emits counts for all four classes, distinct
referenced imported symbol indices, distinct source-defined
module/library/NID triplets, and groups by
`(module local ID, library local ID, raw relocation type)`. Categories
sum to the total relocation count; grouped demand counts sum to the
classified public external-import references.

These are **static relocation references**, not a function-call trace.
Counts do not establish the number of HLE functions implemented,
export availability, correct provider names on real PS5 firmware,
runtime module activation, or title boot. The default opaque dependency
manifest is unchanged. This view retains
`public_demand_resolution=not_attempted`,
`resolution=not_attempted` and `guest_instructions=0`.

Pure owned tests verify that repeated references, distinct relocation
types, reserved symbol 0, defined symbols, unclassified external symbols
and same-NID/different-module identities are never silently conflated.
The pinned independent BlackBear native-title CI verifies its 40 RELA
references, the per-record relationship to the established public symbol
IDs/NID bytes, count conservation and deterministic bounded grouping.

**Implementation follow-up:** use these empirical demand groups to
prioritize tests of generic relocation application and the exact
provider/lifetime policy. Do not interpret a raw type as a supported Sony
relocation automatically or invent a default-success import.
