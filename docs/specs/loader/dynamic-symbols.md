# Dynamic Symbol-Table Discovery and Bounds — M1

**Status:** Proposed  
**Milestone:** M1 — complete  
**Scope:** generic ELF64 dynamic-symbol discovery, bounds, and neutral records

## 1. Purpose

Astraea can now parse bounded `PT_DYNAMIC`, validate dynamic string metadata, resolve initialized guest-image bytes, and resolve bounded dynamic strings.

The next generic ELF layer is the dynamic symbol table referenced by `DT_SYMTAB`.

The core rule is:

> Never discover the end of `DT_SYMTAB` by scanning adjacent mapped bytes or by pointer-distance heuristics.

A symbol table becomes parseable only when Astraea has:

1. a guest address
2. the exact ELF64 symbol-entry width
3. an explicit validated symbol-count source

## 2. Generic tags in scope

Current generic ELF defines:

- `DT_HASH` (4): address of the SysV symbol hash table
- `DT_SYMTAB` (6): address of the dynamic symbol table
- `DT_SYMENT` (11): symbol entry size
- `DT_SYMTAB_SHNDX` (34): optional extended section-index companion
- `DT_SYMTABSZ` (39): total dynamic symbol-table byte size in the current gABI 4.3 draft

`DT_SYMTABSZ` is a recent draft addition. Astraea may support it without assuming historical or PS5-specific binaries contain it.

## 3. Explicit non-solutions

Astraea must not derive symbol count from:

- `DT_STRTAB - DT_SYMTAB`
- nearest higher dynamic pointer
- section-layout coincidence
- reading until a zero-looking symbol
- reading until an unmapped byte
- relocation maximum symbol index alone
- a guessed SCE/PS5 layout

Section headers may later be diagnostic evidence, but they are not an M1 execution-facing dependency.

## 4. Singleton rules

For this validated layer:

- `DT_SYMTAB`
- `DT_SYMENT`
- `DT_SYMTABSZ`
- `DT_HASH`

are singleton metadata.

Identical duplicates are accepted.

Conflicting duplicates fail deterministically with stable source-entry provenance.

Unknown dynamic tags remain preserved in the raw `DynamicTable` and ignored here.

## 5. Metadata presence and companions

If none of `DT_SYMTAB`, `DT_SYMENT`, `DT_SYMTABSZ`, or `DT_HASH` occur, generic dynamic symbol metadata may be represented as absent.

If any occur:

- `DT_SYMTAB` is required
- `DT_SYMENT` is required
- at least one supported symbol-count source is required before symbol parsing

For Astraea's ELF64 profile:

```text
DT_SYMENT == 24
```

Any other value fails with `invalid_symbol_entry_size`.

A non-24-byte record is not silently reinterpreted as a private PS5 format.

## 6. Supported symbol-count evidence

M1 supports two generic count sources.

### 6.1 DT_SYMTABSZ

When present:

- size must be non-zero
- size must be divisible by 24
- `count = DT_SYMTABSZ / 24`
- count must be at least 1 for reserved symbol index 0
- the resulting symbol-table guest range must be representable without overflow

The current gABI draft makes `DT_SYMTABSZ` the direct size source when `DT_HASH` is omitted.

### 6.2 SysV DT_HASH header

A SysV hash table begins with two 32-bit words:

```text
word 0: nbucket
word 1: nchain
```

The gABI defines `nchain` as the dynamic symbol-table entry count.

For **symbol-count discovery only**, Astraea reads exactly this 8-byte header through `InitializedImageView`.

Requirements:

- the entire 8-byte header is deterministically readable
- guest address arithmetic for the header cannot overflow
- `nchain >= 1`
- `nchain * 24` uses checked arithmetic

The symbol layer does **not** scan or validate all `bucket[]` / `chain[]` entries just to obtain the count.

Why:

- symbol parsing only needs the gABI-defined `nchain` field
- a forged huge `nbucket+nchain` must not force work proportional to a claimed hash body
- full bucket/chain validation belongs to a future SysV hash-lookup implementation

The bounded SysV `DT_HASH` reader provides this minimal header evidence.

### 6.3 Multiple sources

If `DT_SYMTABSZ` and a validated SysV `nchain` are both available, they must agree:

```text
DT_SYMTABSZ / 24 == nchain
```

Otherwise descriptor construction fails with `conflicting_symbol_count`.

No source wins by priority.

### 6.4 Unsupported count sources

GNU `DT_GNU_HASH` may provide count evidence, but deriving it safely requires a dedicated bounded validator.

Until implemented:

- preserve the tag in raw metadata
- do not treat it as `DT_HASH`
- do not fall back to pointer-distance heuristics
- if it is the only count mechanism, return `symbol_count_unavailable`/equivalent

PS5/SCE-specific mechanisms remain behind public ABI evidence work in #8.

## 7. Bounded descriptor

Once location, entry width, and count are validated:

```text
DynamicSymbolTableDescriptor
    range: GuestRange
    entry_size: GuestSize   // 24
    count: uint64
    count_sources
    provenance
```

The descriptor contains guest-space metadata only.

It does not contain a host pointer.

Compute:

```text
byte_size = count * 24
range = GuestRange(DT_SYMTAB, byte_size)
```

with checked arithmetic and full-domain guest-range semantics.

No host `size_t` conversion is required merely to describe the guest table.

## 8. ELF64 symbol record

Each symbol record is exactly 24 bytes:

```text
+0   uint32 st_name
+4   uint8  st_info
+5   uint8  st_other
+6   uint16 st_shndx
+8   uint64 st_value
+16  uint64 st_size
```

Decode explicitly from little-endian bytes.

Do not `reinterpret_cast` packed host structs over guest data.

## 9. Neutral DynamicSymbol

Preserve:

```text
DynamicSymbol
    index: uint64
    name_offset: uint32
    info: uint8
    other: uint8
    section_index_raw: uint16
    value: uint64
    size: uint64
```

Raw fields remain authoritative.

Generic convenience accessors may expose:

```text
binding    = info >> 4
type       = info & 0x0f
visibility = other & 0x07
```

The lower **three** visibility bits follow the current generic ELF specification.

No PS5 import/export classification is derived from these fields.

## 10. Indexing and reads

Before reading symbol index `i`:

```text
i < descriptor.count
```

must hold.

Then:

```text
byte_offset = i * 24
entry_address = descriptor.range.base + byte_offset
```

uses checked arithmetic.

The complete 24-byte record is read through `InitializedImageView`.

A partially readable record is an error.

## 11. Reserved symbol index 0

Generic ELF reserves index 0 as `STN_UNDEF`.

A validated generic table must contain at least one symbol, and index 0 must decode to:

- `st_name == 0`
- `st_info == 0`
- `st_other == 0`
- `st_shndx == 0`
- `st_value == 0`
- `st_size == 0`

Violation produces `invalid_undefined_symbol`.

This is a generic ELF conformance check, not PS5-specific interpretation.

## 12. Symbol names

`st_name` remains a raw offset into the validated dynamic string table.

Preferred layering:

```text
DynamicSymbol.name_offset
        ↓
bounded dynamic-string resolver
        ↓
owned symbol name when requested
```

Important:

- `st_name == 0` is valid
- a non-zero name offset is not trusted until resolved
- a bad name offset is a string-resolution failure, not a reason to reinterpret the fixed-width symbol record

## 13. Extended section indexes

If `section_index_raw == SHN_XINDEX`, the real section index lives in companion extended-index metadata.

M1 currently:

- preserves the raw value
- may expose an `is_extended_section_index()` helper
- does not fabricate an effective section index

Bounded `DT_SYMTAB_SHNDX` support is a later issue if required.

## 14. Import/export boundary

Generic ELF facts do not define PS5 semantics.

Specifically:

- `SHN_UNDEF` does not automatically mean PS5 HLE import
- a defined symbol does not automatically mean PS5 export
- symbol strings do not yet define canonical NIDs
- generic binding/type/visibility do not establish SCE module identity

Those interpretations remain behind public ABI evidence work in #8 and later hardware/public-evidence work.

## 15. Error model

Stable categories should include at least:

- `conflicting_dynamic_tag`
- `missing_required_companion_tag`
- `invalid_symbol_entry_size`
- `invalid_symbol_table_size`
- `symbol_count_unavailable`
- `conflicting_symbol_count`
- `symbol_table_size_overflow`
- `symbol_table_range_overflow`
- `hash_header_range_overflow`
- `hash_header_unreadable`
- `invalid_hash_symbol_count`
- `symbol_index_out_of_bounds`
- `symbol_entry_unreadable`
- `invalid_undefined_symbol`

Nested `InitializedImageError` should be preserved where applicable.

Human-readable text is not the stable API.

## 16. Determinism

Equivalent input must produce:

- identical singleton/conflict behavior
- identical symbol count
- identical descriptor
- identical symbol records ordered strictly by index
- identical typed error categories

No hash-map or tag iteration order may select a winner.

## 17. Security properties

- no symbol parsing without explicit count evidence
- no pointer-distance symbol sizing
- no unchecked `count * 24`
- no unchecked `index * 24`
- no guest integer-to-pointer cast
- no partial symbol-record decode
- no work proportional to untrusted SysV `nbucket+nchain` merely to discover count
- no unbounded allocation from a symbol count
- no PS5 semantics inferred from generic fields

## 18. Testing matrix

### Metadata / descriptor

- no symbol tags -> absent metadata
- valid `DT_SYMTABSZ` count
- valid SysV `nchain` count
- both count sources agree
- both count sources conflict
- identical singleton duplicates
- conflicting singleton duplicates
- missing `DT_SYMTAB`
- missing `DT_SYMENT`
- invalid `DT_SYMENT`
- zero/non-multiple `DT_SYMTABSZ`
- symbol-table guest-range overflow

### SysV count header

- minimal readable 8-byte header
- `nchain == 0` rejection
- header crossing compatible mapping boundary
- unmapped/truncated header
- header guest-range overflow
- huge `nbucket` does not cause proportional scan/allocation
- large `nchain` is handled only via checked symbol-table range arithmetic

### Symbols

- valid reserved index 0
- malformed reserved index 0
- index == count rejection
- final valid index
- record crossing adjacent compatible mappings
- nested image-read failure
- binding/type/3-bit visibility accessors
- raw OS/processor-specific values preserved
- `SHN_XINDEX` preserved unresolved

## 19. Implementation sequence

1. Implement bounded SysV `DT_HASH` header / `nchain` evidence
2. build symbol-table metadata with `DT_SYMTAB`, `DT_SYMENT`, optional `DT_SYMTABSZ`, and count-source agreement
3. Parse bounded neutral `Elf64_Sym` records
4. resolve symbol names lazily with the existing dynamic-string resolver
5. add GNU-hash count derivation only if required by fixtures/evidence
6. feed bounded symbols into generic relocation metadata later

## 20. Out of scope

- GNU hash count inference
- SysV hash lookup
- inter-module symbol resolution
- relocation application
- PS5 NID encoding
- PS5 import/export classification
- SCE-specific dynamic tags
- HLE binding
- effective `DT_SYMTAB_SHNDX` resolution

## 21. Evidence

Primary generic evidence:

- current System V ELF Object File Format / gABI dynamic-linking and symbol-table chapters
- x86-64 psABI where processor-specific semantics are later required

The current gABI draft introduces `DT_SYMTABSZ`; Astraea treats this as supported generic metadata without assuming older binaries contain it.

These sources define generic ELF behavior only, not undocumented PS5/SCE semantics.

## 22. Decision summary

For M1:

> A dynamic symbol table is parseable only when its guest address, exact 24-byte ELF64 entry width, and explicit symbol count are known. Astraea supports current-gABI DT_SYMTABSZ and bounded SysV DT_HASH:nchain evidence, requires agreement when both exist, and never guesses the table end from surrounding mapped memory.
