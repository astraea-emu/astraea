# Relocation and Module Metadata Model — M1

**Status:** Proposed  
**Milestone:** M1 — complete  
**Scope:** neutral representation of ELF dynamic-link metadata before PS5-specific resolution semantics

## 1. Purpose

M1 needs a durable representation for information that will later drive relocation, dependency discovery, symbol lookup, and module loading.

This specification intentionally separates three layers:

1. **Raw ELF metadata** — values that can be decoded from generic ELF structures.
2. **Validated neutral metadata** — ranges, tables, symbols, and relocations whose structural relationships have been checked.
3. **Platform resolution policy** — PS5-specific module identity, import/export encoding, NIDs, custom dynamic tags, relocation behavior, and HLE binding.

M1 defines layers 1 and 2. Layer 3 requires separate provenance-backed research.

## 2. Source of execution metadata

Astraea's execution-facing loader must not require section headers.

For `ET_EXEC` / `ET_DYN` images, dynamic-link metadata should be discoverable from program-header-visible state, beginning with `PT_DYNAMIC`.

Section headers may later provide diagnostics or corroborating information, but they are not the authoritative runtime dependency.

## 3. Raw dynamic entries

An ELF64 dynamic entry is represented neutrally as:

```text
DynamicEntry
    tag:   int64
    value: uint64
    index: stable entry index
```

M1 does not encode `d_val` versus `d_ptr` into the raw representation because the meaning is determined by `d_tag`.

Requirements:

- preserve entry order
- preserve unknown OS-/processor-specific tags
- stop semantic table scanning at the first `DT_NULL`
- bytes after the terminating `DT_NULL` are not silently interpreted as additional dynamic entries
- duplicate tags remain representable until a validation rule declares them contradictory

Unknown tags are evidence, not errors by default.

## 4. Dynamic table discovery

A validated `PT_DYNAMIC` record may identify the dynamic-entry byte range.

The implementation that eventually parses this table must verify:

- the guest/file-backed bytes needed for the table are actually available
- entry alignment and entry width are valid for ELF64
- scanning cannot run beyond the validated containing range
- a missing `DT_NULL` is reported as a structural error rather than scanning arbitrary following bytes

M1 does not assume that a section named `.dynamic` exists.

## 5. Dynamic tag classification

Known generic tags should be classified by semantic category without discarding the raw value.

### 5.1 Table/location tags

Examples:

- `DT_STRTAB`
- `DT_SYMTAB`
- `DT_RELA`
- `DT_REL`
- `DT_RELR`
- `DT_JMPREL`
- `DT_HASH`
- `DT_GNU_HASH` when independently specified

These identify guest addresses or table locations whose bytes must later be resolved through the guest-image mapping model.

A guest address is not a host pointer.

### 5.2 Size/stride tags

Examples:

- `DT_STRSZ`
- `DT_SYMENT`
- `DT_RELASZ`
- `DT_RELAENT`
- `DT_RELSZ`
- `DT_RELENT`
- `DT_RELRSZ`
- `DT_RELRENT`
- `DT_PLTRELSZ`

Sizes and entry widths must use checked arithmetic and must be validated as a coherent pair with the table they describe.

### 5.3 String-reference tags

Examples:

- `DT_NEEDED`
- `DT_SONAME`
- `DT_RPATH`
- `DT_RUNPATH`

The raw value is an offset into the dynamic string table, not a C string pointer.

A neutral representation stores the offset first. String resolution occurs only after `DT_STRTAB` and `DT_STRSZ` have been validated.

### 5.4 Behavioral/control tags

Examples:

- `DT_FLAGS`
- `DT_FLAGS_1`
- `DT_BIND_NOW`
- initialization/finalization array tags

These remain generic metadata until a later subsystem needs their semantics.

### 5.5 OS-/processor-specific tags

Preserve exactly.

Do not infer PS5 semantics merely because another project assigns a name to the value.

## 6. Validated table descriptors

A validated metadata layer should represent table **descriptors**, not raw pointers.

Conceptually:

```text
GuestTableDescriptor
    guest_address: GuestAddress
    byte_size:     GuestSize
    entry_size:    GuestSize
    source_tag(s): stable provenance
```

A descriptor is valid only if:

- its address/range is representable
- byte size and entry size are coherent
- an entry-sized table divides evenly when the format requires fixed-width entries
- the guest range can later be resolved to initialized bytes through the mapping/image model

The descriptor itself does not expose host memory.

## 7. Dynamic string references

Represent a dynamic string reference as:

```text
DynamicStringRef
    offset: uint64
```

Resolving it requires a validated dynamic string-table descriptor.

Validation must ensure:

- offset < `DT_STRSZ`
- a NUL terminator exists before the end of the validated string table
- the returned string view cannot outlive its backing image

M1 should not copy arbitrary strings merely to make them safe; a bounded owned representation or lifetime-safe view may be chosen at implementation time.

## 8. Symbol metadata

The neutral ELF64 dynamic-symbol representation should preserve at least:

```text
DynamicSymbol
    index
    name_offset
    info
    other
    section_index
    value
    size
```

Convenience accessors may decode generic ELF fields such as binding and type, but raw fields remain available.

Important boundary:

- an undefined dynamic symbol is **not automatically a PS5 HLE import**
- a defined dynamic symbol is **not automatically a public PS5 export**

Those classifications depend on PS5-specific module/ABI evidence.

## 9. Module/dependency metadata

Generic ELF can express dependency string references such as `DT_NEEDED`.

M1 therefore defines a neutral dependency record conceptually as:

```text
ModuleDependency
    dynamic_entry_index
    name: DynamicStringRef
```

Do not yet define the canonical PS5 module identity from the dependency string.

Likewise, `DT_SONAME` may be represented as generic ELF metadata but must not be assumed to be the complete PS5 module key.

## 10. Relocation representation

Astraea needs one neutral relocation record capable of representing REL, RELA, and future supported packed/relative forms without conflating their semantics.

Conceptually:

```text
RelocationRecord
    source_kind
    target: GuestAddress
    type: uint32
    symbol_index: optional<uint32>
    addend: optional<int64>
    source_index
```

### 10.1 RELA

For ELF64 `Rela`:

- target comes from `r_offset`
- symbol index/type are decoded from `r_info` according to the x86-64 ELF psABI
- explicit signed addend comes from `r_addend`

### 10.2 REL

For ELF64 `Rel`:

- target comes from `r_offset`
- symbol index/type are decoded from `r_info`
- there is no explicit addend in the record

A later relocation engine may need to obtain an implicit addend from target memory according to the relocation type. M1 metadata parsing must not guess that behavior.

### 10.3 RELR

RELR is a compact encoding of relative relocation offsets.

Do not prematurely expand RELR while merely preserving raw dynamic metadata. If/when RELR parsing is implemented, expansion must be bounded by the validated RELR table and produce deterministic relocation targets.

### 10.4 PLT/JMPREL

`DT_JMPREL` describes a relocation table whose entry encoding is selected by `DT_PLTREL`.

The metadata model should distinguish its provenance (for example, PLT/JMPREL versus general relocation table) without implying lazy/eager binding policy.

## 11. Relocation type semantics

Relocation **type numbers** are preserved even if Astraea does not implement them.

The x86-64 psABI is the primary source for generic x86-64 relocation type semantics.

M1 metadata parsing may decode the type number and referenced symbol index. It must not apply a relocation merely because its numeric type is recognized.

Application belongs to a later relocation engine with:

- explicit target range checks
- symbol-resolution input
- checked arithmetic
- write-width validation
- provenance-backed handling of PS5-specific relocation types

Unsupported relocation types should be data until application is attempted.

## 12. Duplicate/conflicting dynamic tags

The raw table preserves duplicates.

The validated metadata builder should apply explicit rules:

- naturally repeated tags such as `DT_NEEDED` remain ordered repeated records
- singleton table-location/size/stride tags may be accepted only when duplicate values agree
- contradictory singleton values produce a typed metadata error
- unknown tags are never deduplicated by value

This prevents unordered-map insertion order from silently selecting behavior.

## 13. Error model

The future metadata parser/builder should expose stable typed categories including at least:

- `dynamic_table_out_of_bounds`
- `dynamic_table_missing_terminator`
- `dynamic_entry_overflow`
- `conflicting_dynamic_tag`
- `missing_required_companion_tag`
- `invalid_table_entry_size`
- `table_size_not_multiple_of_entry_size`
- `guest_table_range_overflow`
- `unmapped_guest_table`
- `dynamic_string_offset_out_of_bounds`
- `dynamic_string_unterminated`
- `symbol_index_out_of_bounds`
- `relocation_info_invalid`
- `unsupported_relocation_encoding`

PS5-specific resolver errors belong to a later layer.

## 14. Determinism

Equivalent input must produce stable ordering independent of hash-map iteration.

Recommended ordering:

- dynamic entries: file/table order
- dependencies: dynamic-entry order
- symbols: symbol-table index
- relocations: source table identity, then source entry index

Do not sort away source ordering merely for convenience.

## 15. Security properties

- no ELF-provided address becomes a host pointer by cast
- no count/size multiplication occurs without checked arithmetic
- no table is iterated until its entire required byte range is validated
- no dynamic string is read without a bounded terminator search
- no symbol/relocation index is trusted before table-bound checks
- metadata parsing never mutates guest memory
- relocation application is a separate phase from relocation parsing

## 16. M1 implementation sequence

After this specification is accepted:

1. discover/parse bounded `PT_DYNAMIC` entries
2. build raw `DynamicEntry` sequence
3. validate generic table descriptors
4. expose bounded dynamic strings
5. parse neutral dynamic symbols
6. parse neutral RELA/REL metadata as required by fixtures
7. preserve RELR/custom encodings until separately implemented
8. only then connect relocation application/resolution

Each step should have synthetic ELF fixtures and malformed-input tests.

## 17. PS5-specific research boundary

The following are explicitly **not defined by this document**:

- PS5 canonical module identity
- NID encoding/lookup
- custom Sony/FreeBSD-derived dynamic tags as used by PS5
- PS5 import/export classification rules
- system-module search order
- HLE binding precedence
- PS5-specific relocation types or deviations
- firmware/system-library compatibility policy

Those questions belong to public ABI evidence work in #8 and later probe-backed research.

Astraea should preserve unknown data needed to answer them later rather than discarding it now.

## 18. Primary generic evidence

Generic ELF behavior should be grounded in:

- System V ELF gABI, including Dynamic Linking, Symbol Table, and Relocation chapters: https://gabi.xinuos.com/elf.pdf
- x86-64 ELF psABI for `r_info` layout and x86-64 relocation semantics: https://gitlab.com/x86-psABIs/x86-64-ABI

These sources define generic ELF/x86-64 behavior only. They are not evidence for undocumented PS5 extensions.

## 19. Decision summary

For M1:

> Parse and preserve generic ELF dynamic metadata into host-independent, deterministic records; resolve guest-addressed tables only through validated mapping/image abstractions; keep parsing separate from relocation application; and refuse to define PS5 module/import/export semantics before provenance exists.
