# Bounded Dynamic Relocation Metadata — M1

**Status:** Proposed  
**Milestone:** M1 — complete  
**Scope:** generic ELF64 REL / RELA / PLT relocation metadata before relocation application

## 1. Purpose

Astraea now has bounded generic ELF64 dynamic symbols.

The next M1 layer is dynamic relocation **metadata**: safely describing and decoding relocation records without applying them.

This layer answers:

- where each relocation table is
- which encoding it uses
- how many records it contains
- which guest address each record targets
- which symbol index/type each record names
- whether an explicit addend exists

It does **not** answer:

- what a processor-specific relocation type means
- how many bytes it writes
- how to resolve a symbol
- how to mutate guest memory

Those belong to later stages.

## 2. Primary generic evidence

Current generic ELF defines:

### Elf64_Rel

```text
r_offset  Elf64_Addr   8 bytes
r_info    Elf64_Xword  8 bytes
```

Total: **16 bytes**

### Elf64_Rela

```text
r_offset  Elf64_Addr    8 bytes
r_info    Elf64_Xword   8 bytes
r_addend  Elf64_Sxword  8 bytes
```

Total: **24 bytes**

For executable/shared objects, `r_offset` is the virtual address of the storage unit affected by the relocation.

For ELF64:

```text
symbol_index    = r_info >> 32
relocation_type = r_info & 0xffffffff
```

`REL` records contain no explicit addend; the addend is implicit in the location to be modified.

`RELA` records carry a signed explicit `r_addend`.

## 3. Dynamic tags in scope

### General RELA table

- `DT_RELA` — guest address
- `DT_RELASZ` — total byte size
- `DT_RELAENT` — entry size

### General REL table

- `DT_REL` — guest address
- `DT_RELSZ` — total byte size
- `DT_RELENT` — entry size

### PLT/JMPREL table

- `DT_JMPREL` — guest address of relocations associated solely with the procedure linkage table
- `DT_PLTRELSZ` — total byte size
- `DT_PLTREL` — encoding selector whose value is `DT_REL` or `DT_RELA`

### RELR

- `DT_RELR`
- `DT_RELRSZ`
- `DT_RELRENT`

RELR is explicitly preserved as a **separate future encoding**. It is not parsed as REL or RELA.

## 4. Table kinds

Astraea must preserve relocation-table provenance.

Conceptually:

```text
RelocationTableKind
    rel
    rela
    plt_rel
    plt_rela
```

General and PLT tables remain distinct even if they:

- use the same encoding
- overlap in guest address space
- contain identical records

No metadata-stage deduplication or flattening is allowed.

## 5. Singleton duplicate rules

For the validated generic layer, the following tags are singleton metadata:

- `DT_RELA`
- `DT_RELASZ`
- `DT_RELAENT`
- `DT_REL`
- `DT_RELSZ`
- `DT_RELENT`
- `DT_JMPREL`
- `DT_PLTRELSZ`
- `DT_PLTREL`

Identical duplicate values are accepted.

Conflicting duplicate values fail with `conflicting_dynamic_tag` and stable source-entry provenance.

Unknown/OS-/processor-specific tags remain preserved by the raw `DynamicTable`.

## 6. General RELA activation and companions

If any of:

- `DT_RELA`
- `DT_RELASZ`
- `DT_RELAENT`

is present, the general RELA metadata group is active.

All three are required.

For ELF64:

```text
DT_RELAENT == 24
```

Otherwise fail with `invalid_relocation_entry_size`.

`DT_RELASZ` must be divisible by 24.

A zero-size table is representable as an empty descriptor with count 0.

For non-zero size:

```text
count = DT_RELASZ / 24
range = GuestRange(DT_RELA, DT_RELASZ)
```

The guest range must be representable without overflow.

## 7. General REL activation and companions

If any of:

- `DT_REL`
- `DT_RELSZ`
- `DT_RELENT`

is present, the general REL metadata group is active.

All three are required.

For ELF64:

```text
DT_RELENT == 16
```

Otherwise fail with `invalid_relocation_entry_size`.

`DT_RELSZ` must be divisible by 16.

A zero-size table is representable as an empty descriptor with count 0.

For non-zero size:

```text
count = DT_RELSZ / 16
range = GuestRange(DT_REL, DT_RELSZ)
```

The guest range must be representable without overflow.

## 8. PLT/JMPREL activation and companions

If any of:

- `DT_JMPREL`
- `DT_PLTRELSZ`
- `DT_PLTREL`

is present, the PLT relocation metadata group is active.

All three are required.

`DT_PLTREL` must equal exactly one of the generic dynamic-tag values:

- `DT_REL`
- `DT_RELA`

It selects the encoding for the whole PLT relocation table.

Therefore:

```text
DT_PLTREL == DT_REL  -> entry_size = 16, kind = plt_rel
DT_PLTREL == DT_RELA -> entry_size = 24, kind = plt_rela
```

`DT_PLTRELSZ` must be divisible by the selected entry size.

A zero-size PLT relocation table is representable as empty.

The table range uses `DT_JMPREL` as its guest base.

The PLT relocation layer builds on the common REL/RELA decoder.

## 9. RELR boundary

If any RELR tag occurs, Astraea preserves that fact for later work but does not reinterpret the table as REL/RELA.

The current gABI requires:

- `DT_RELR`
- `DT_RELRSZ`
- `DT_RELRENT`

as a companion group, and specifies that RELR is processed before REL/RELA during dynamic linking.

M1 relocation metadata does not expand RELR.

A future issue must define its own bounded decoder.

## 10. Table descriptor

A neutral descriptor conceptually contains:

```text
DynamicRelocationTableDescriptor
    kind: RelocationTableKind
    range: GuestRange
    entry_size: GuestSize
    count: uint64
    address_source_entry_index
    size_source_entry_index
    encoding_source_entry_index   // RELENT/RELAENT/PLTREL as appropriate
```

The descriptor contains guest metadata only.

No host pointer or writable memory is exposed.

## 11. Neutral relocation record

A decoded record conceptually contains:

```text
DynamicRelocation
    table_kind
    table_index
    target: GuestAddress
    raw_info: uint64
    symbol_index: uint32
    relocation_type: uint32
    addend: optional<int64>
```

### REL

- `addend = nullopt`
- parser does not inspect the target bytes to obtain the implicit addend

### RELA

- `addend` is present
- the 64-bit stored bit pattern is decoded as signed `Elf64_Sxword`
- implementation must preserve the bit pattern; do not rely on implementation-defined unsigned-to-signed narrowing

## 12. r_offset semantics

For the executable/shared-object profile used by Astraea's M1 dynamic loader:

`r_offset` is represented directly as `GuestAddress`.

A value of `UINT64_MAX` is therefore representable as a target address.

The metadata parser must **not** construct a target write range yet.

Reason:

- relocation write width is processor/type specific
- some relocations do not write ordinary scalar widths
- type semantics belong to the x86-64 relocation-application layer

Target writability/mapping is therefore not a metadata-stage validity condition.

## 13. r_info semantics

For ELF64:

```text
symbol_index = uint32(r_info >> 32)
type         = uint32(r_info & 0xffffffff)
```

Store `raw_info` as well.

Do not constrain generic relocation-type numbers at this stage.

Processor-specific or OS-specific relocation types remain data.

## 14. Symbol-index validation

Every decoded REL/RELA record validates:

```text
symbol_index < DynamicSymbolTableDescriptor.symbol_count
```

Index 0 is valid and represents `STN_UNDEF`.

If no bounded dynamic-symbol descriptor is available while relocation records are present, parsing fails with `missing_symbol_table`.

Why validate even for relocation types that may not semantically require a symbol?

At the generic metadata layer, `r_info` still encodes a symbol-table index. Index 0 is the generic no-symbol/undefined sentinel. Any non-zero out-of-range index is malformed relative to the associated dynamic symbol table.

Processor-specific semantics may later decide that a given type ignores the symbol value, but they cannot make an out-of-range table index safe to dereference.

## 15. Lazy indexed reads

Descriptors are cheap metadata.

Records are read lazily:

```text
parse_dynamic_relocation(descriptor, index, symbols, image_view)
```

Before any multiplication/read:

```text
index < descriptor.count
```

Then checked arithmetic computes:

```text
byte_offset = index * entry_size
record_address = descriptor.range.base + byte_offset
```

The complete 16- or 24-byte record is read through `InitializedImageView`.

Partially readable records fail.

No vector proportional to relocation count is required merely to construct a descriptor.

## 16. Multiple tables and ordering

An ELF object may expose both general REL and general RELA metadata.

Astraea preserves them as independent descriptors.

Stable presentation order for generic metadata:

1. general REL
2. general RELA
3. PLT REL / PLT RELA

Within each table, source order is numeric table index.

This ordering is an Astraea deterministic presentation rule; it does not assert relocation-application order beyond what generic ELF specifies.

## 17. Overlapping tables

Relocation table guest ranges may overlap.

Metadata construction does not:

- merge them
- deduplicate entries
- pick a preferred table
- reject overlap solely because it exists

Each descriptor is independently bounded and provenance-preserving.

If overlapping descriptors cause contradictory behavior during later application, that belongs to the relocation execution policy, not raw metadata parsing.

## 18. Implicit REL addends

For `REL`, the addend resides at the relocation target according to processor-specific relocation semantics.

Metadata parsing must not read it.

Why:

- width/interpretation depends on relocation type
- reading target bytes here would prematurely entangle generic parsing with x86-64 semantics
- relocation metadata must remain read-only

The later application layer may obtain the implicit addend after it knows the relocation type and required field width.

## 19. Relocation application boundary

This specification does **not** apply relocations.

In particular it does not:

- resolve symbol values
- compute load bias
- evaluate x86-64 relocation expressions
- read implicit addends
- validate target write width
- check final numeric overflow/truncation
- mutate initialized-image or runtime guest memory
- modify GOT/PLT state
- choose lazy/eager binding behavior

Parsing and mutation remain separate phases.

## 20. Error model

Stable categories should include at least:

- `conflicting_dynamic_tag`
- `missing_required_companion_tag`
- `invalid_relocation_entry_size`
- `invalid_relocation_table_size`
- `relocation_table_range_overflow`
- `invalid_plt_relocation_encoding`
- `missing_symbol_table`
- `relocation_index_out_of_bounds`
- `relocation_entry_unreadable`
- `relocation_symbol_index_out_of_bounds`

Errors should preserve:

- relevant dynamic tag
- source-entry indexes
- relocation table kind
- relocation table index where applicable
- guest address where applicable
- nested `InitializedImageError` for read failures

Human-readable strings are not the stable API.

## 21. Security properties

- no packed host-struct casts
- no table scanning without declared byte size
- no unchecked table-size / count / index arithmetic
- no record read outside declared range
- no host pointer created from guest address
- no vector allocation proportional to untrusted relocation count merely to describe a table
- no implicit-addend read during metadata parsing
- no relocation write during metadata parsing
- no processor-specific relocation operation executed at metadata stage
- no PS5/SCE relocation semantics inferred from generic records

## 22. Testing matrix

### General RELA metadata

- absent group
- valid empty table
- valid one/multiple records
- missing each companion
- identical duplicate tags
- conflicting duplicate tags
- wrong `DT_RELAENT`
- non-divisible `DT_RELASZ`
- guest-range overflow
- table ending at `UINT64_MAX`

### General REL metadata

- same cases with 16-byte records

### Record decoding

- first/final valid index
- index == count rejection
- record crossing adjacent compatible mappings
- nested initialized-image failure
- `r_offset == UINT64_MAX`
- symbol index 0
- highest valid symbol index
- symbol index == symbol_count rejection
- raw 32-bit relocation type preserved
- REL has no addend
- RELA addend 0
- RELA positive addend
- RELA negative addend
- RELA `INT64_MIN` bit pattern

### PLT

- REL-selected PLT table
- RELA-selected PLT table
- invalid `DT_PLTREL`
- missing companions
- size divisibility
- provenance stays `plt_rel` / `plt_rela`
- overlap with general table does not deduplicate

## 23. Implementation sequence

1. General REL/RELA descriptor construction + neutral lazy decoder
2. PLT/JMPREL descriptor using the same decoder
3. future bounded RELR decoder if required
4. x86-64 relocation semantics/application only after neutral metadata is stable
5. PS5/SCE-specific relocation extensions only after public ABI evidence work in #8 and probe-backed evidence

## 24. Evidence

Primary generic evidence:

- System V ELF Object File Format 4.3 draft, Chapter 6 (Relocation)
- System V ELF Object File Format 4.3 draft, Section 8.3 (Dynamic Section)
- x86-64 psABI for later processor-specific relocation **semantics**, not for this generic metadata layer

## 25. Decision summary

For M1:

> Astraea treats REL, RELA, and PLT relocation tables as bounded read-only metadata. Every record is decoded lazily through the initialized-image view, preserves table provenance and raw ELF fields, validates its symbol index against the bounded dynamic symbol table, and performs no relocation operation or guest-memory mutation.
