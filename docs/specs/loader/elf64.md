# ELF64 Loader Invariants — M1

**Status:** Proposed for M1 implementation  
**Issue:** #3  
**Scope:** structural parsing and validation of synthetic/owned ELF64 guest images

## 1. Purpose

This specification defines Astraea's first executable-container contract. It intentionally covers generic ELF64 structure before PS5-specific module semantics.

The parser is a security boundary: inputs are untrusted byte sequences. The implementation must not rely on packed-struct casts, host alignment, host endianness, or unchecked pointer/integer arithmetic.

## 2. M1 supported profile

M1 accepts the following baseline profile:

- ELF magic: `0x7f 'E' 'L' 'F'`
- class: `ELFCLASS64`
- data encoding: `ELFDATA2LSB`
- identification version: `EV_CURRENT`
- ELF header version: `EV_CURRENT`
- machine: `EM_X86_64`
- file type: `ET_EXEC` or `ET_DYN`
- standard ELF64 header size: 64 bytes
- standard ELF64 program-header entry size: 56 bytes

`EI_OSABI`, `EI_ABIVERSION`, and `e_flags` are preserved as metadata and are not assigned PS5 semantics in M1.

Unknown/OS-specific/processor-specific program-header types are preserved as generic program-header records rather than rejected solely because Astraea does not yet understand their semantics.

## 3. Explicitly unsupported in M1

The parser returns a dedicated unsupported-feature error for:

- ELF32
- big-endian ELF
- non-x86-64 machine types
- relocatable/core files as executable guest images
- extended program-header numbering via `PN_XNUM`
- non-standard ELF64 header/program-header entry sizes

These choices are implementation-scope limits, not claims that such files are invalid ELF.

## 4. Parsing model

### 4.1 Byte access

All multi-byte fields are decoded from a bounded byte view using explicit little-endian reads.

Forbidden implementation techniques:

- `reinterpret_cast<Elf64_Ehdr*>` onto file bytes
- unaligned typed loads
- pointer arithmetic before validating the corresponding integer range
- arithmetic that can wrap before a bounds check

### 4.2 Checked arithmetic

Every expression that combines file-controlled values must use checked arithmetic.

At minimum:

- `e_phnum * e_phentsize`
- `e_phoff + program_header_table_size`
- `p_offset + p_filesz`
- if `p_memsz > 0`, `p_vaddr + (p_memsz - 1)`

Overflow is a validation failure even if a later truncating conversion would appear to place the result inside the file.

### 4.3 Host-size conversion

A validated 64-bit file quantity may be converted to `size_t` only after proving it is representable on the host and within the backing byte view.

## 5. ELF header validation order

The implementation should fail deterministically in this order where practical:

1. minimum bytes for `e_ident`
2. magic
3. class
4. data encoding
5. identification version
6. minimum bytes for complete ELF64 header
7. `e_version`
8. `e_machine`
9. supported `e_type`
10. `e_ehsize`
11. program-header numbering/entry size
12. program-header table range

This ordering makes malformed-input tests and future fuzz regressions stable.

## 6. Program-header table invariants

Given:

- `phoff = e_phoff`
- `phentsize = e_phentsize`
- `phnum = e_phnum`

A file with `phnum == 0` is structurally parseable, but it does not by itself produce a runnable guest image. The later guest-image validation layer may require loadable segments.

If `phnum > 0`:

- `phoff` must identify a range fully contained in the file.
- `phentsize` must equal 56 for the M1 profile.
- `phnum == PN_XNUM` is rejected as unsupported in M1.
- the multiplication and addition required to derive the table range must not overflow.

The parser iterates by validated offsets, not by pointer-incrementing a host struct.

## 7. Generic program-header representation

Every parsed entry records at least:

- `p_type`
- `p_flags`
- `p_offset`
- `p_vaddr`
- `p_paddr`
- `p_filesz`
- `p_memsz`
- `p_align`
- original program-header index

Unknown `p_type` values remain representable.

Program-header fields are first preserved as raw validated-width values. File-backed byte views are exposed only for segment types whose semantics Astraea understands.

In particular, `PT_NULL` has undefined values in its other members and those members must not cause range/alignment validation failures.

For known segment types whose generic ELF semantics describe file-backed content, any non-zero file range must be representable and fully contained in the file before later code may expose bytes for that segment. Unknown OS-/processor-specific segment types remain representable, but M1 does not manufacture a file view or semantic interpretation from their raw `p_offset/p_filesz` fields.

`p_paddr` is preserved but ignored by M1 guest mapping.

## 8. PT_LOAD validation

For every `PT_LOAD` entry:

### 8.1 File/memory sizes

- `p_filesz <= p_memsz`
- both may be zero
- if `p_filesz > 0`, the file range must be in bounds
- the highest included guest address for `p_memsz` must not overflow

When materialized later, bytes `[0, p_filesz)` come from the file and bytes `[p_filesz, p_memsz)` are zero-filled.

### 8.2 Alignment

- `p_align == 0` or `p_align == 1` means no alignment requirement
- otherwise `p_align` must be a power of two
- otherwise `p_vaddr % p_align == p_offset % p_align`

No host page-size assumption belongs in this structural parser.

### 8.3 Ordering

For the M1 supported profile, `PT_LOAD` entries must appear in non-decreasing `p_vaddr` order, matching the generic ABI requirement.

If later PS5 evidence demonstrates accepted non-conforming ordering, that should be handled by an explicit policy change/ADR rather than silently weakening this invariant.

### 8.4 Overlap

Structural parsing does **not** reject two `PT_LOAD` entries merely because their virtual ranges overlap or share a page.

ELF loading may involve page-granular relationships between adjacent segments. The exact guest mapping/permission conflict policy belongs to issue #5 and must not be guessed here.

The parser should nevertheless expose enough information for the mapping layer to detect and reason about overlaps deterministically.

## 9. Entry point

`e_entry` is parsed and preserved.

M1 structural parsing does not require `e_entry` to be non-zero and does not yet prove that the entry address lies within an executable loadable segment. Runnable-image validation can add that requirement once the mapping model exists.

## 10. Sections are not required for M1 execution parsing

The M1 loader does not require a valid section-header table to parse the program image.

Section metadata may be added later for symbols, diagnostics, relocations, or research, but execution-facing parsing must not assume sections exist.

Extended numbering features that require section-header zero are therefore explicitly unsupported until section parsing is specified.

## 11. Error model

The parser should return typed errors with stable categories rather than free-form exception strings.

Initial categories should cover at least:

- `file_too_small`
- `bad_magic`
- `unsupported_class`
- `unsupported_endianness`
- `unsupported_ident_version`
- `unsupported_elf_version`
- `unsupported_machine`
- `unsupported_file_type`
- `invalid_elf_header_size`
- `invalid_program_header_entry_size`
- `unsupported_extended_program_header_count`
- `integer_overflow`
- `program_header_table_out_of_bounds`
- `segment_file_range_out_of_bounds`
- `load_file_size_exceeds_memory_size`
- `load_virtual_range_overflow`
- `invalid_load_alignment`
- `load_segments_out_of_order`

Errors should carry the relevant program-header index/offset when available, but human-readable text is not the stable API.

## 12. Fixture/test matrix

The implementation issue (#4) is not complete without fixtures/tests for at least:

### Valid

- minimal ELF64 with zero program headers (structural parse only)
- one RX `PT_LOAD`
- separate RX and RW `PT_LOAD` entries
- `p_memsz > p_filesz` zero-fill case
- `p_filesz == 0`
- `p_align == 0`
- `p_align == 1`
- unknown OS-specific `p_type` preserved
- non-zero OSABI/ABI version preserved without interpretation

### Invalid / unsupported

- truncation at every ELF-header byte boundary
- bad each magic byte
- ELF32
- big-endian
- invalid/unsupported versions
- wrong machine
- unsupported file type
- wrong header size
- wrong program-header entry size
- `PN_XNUM`
- multiplication overflow
- addition overflow
- program-header table truncation
- known file-backed segment range truncation
- `PT_NULL` with arbitrary non-type fields is preserved/ignored rather than rejected
- `p_filesz > p_memsz`
- virtual-range overflow
- non-power-of-two `p_align > 1`
- incongruent `p_vaddr` / `p_offset`
- descending `PT_LOAD p_vaddr`

### Fuzzing properties

For arbitrary input:

- no crash
- no UB under sanitizer instrumentation
- no out-of-bounds access
- no allocation based on unchecked file values
- deterministic success/error result for identical bytes
- successful parse can be revalidated without touching bytes outside the input view

## 13. Security/performance constraints

- Parsing complexity must be linear in the program-header count.
- Do not allocate `p_memsz` bytes while parsing headers.
- Do not allocate a vector using `e_phnum` until the full program-header table range is validated.
- File-backed views must remain bounded by the validated original input.
- No parser result may contain raw pointers whose lifetime is ambiguous.

## 14. Evidence

Primary generic ELF references:

- Xinuos ELF gABI, ELF Header: https://gabi.xinuos.com/elf/02-eheader.html
- Xinuos ELF gABI, Program Loading / Program Header: https://gabi.xinuos.com/elf/07-pheader.html

Relevant normative points from the gABI include:

- the ELF header identifies class, encoding, machine, version, and program-header table location/entry count
- `e_phentsize * e_phnum` defines the program-header table size
- `PT_LOAD p_filesz` may not exceed `p_memsz`
- extra load-segment memory beyond `p_filesz` is zero-filled
- alignment 0/1 means no requirement; larger `p_align` values are powers of two with congruent virtual/file offsets
- loadable segments are specified in ascending virtual-address order

## 15. PS5-specific boundary

This document does **not** claim that every PS5 executable is fully described by generic System V ELF semantics.

PS5-specific executable/module extensions, OS-specific program-header types, dynamic tags, relocations, import/export encodings, and ABI behavior require separate provenance-backed specifications.

The M1 parser should therefore preserve unknown metadata wherever doing so is safe, while refusing to invent semantics for it.
