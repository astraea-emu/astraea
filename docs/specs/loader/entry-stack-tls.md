# Synthetic x86-64 Entry Stack and Generic TLS Template — M1

**Status:** Proposed  
**Issue:** #48  
**Scope:** portable construction primitives for controlled synthetic guest entry

## 1. Purpose

M1 needs deterministic primitives that let a later execution layer enter a synthetic owned x86-64 executable with:

- a valid generic initial process stack image
- optional generic ELF TLS template metadata/content

This document deliberately separates **generic ABI construction** from **PS5 process bootstrap behavior**.

Nothing here claims that a retail PS5 process uses the same auxv contents, thread-control-block layout, register values, loader policy, or TLS module allocator.

## 2. Primary evidence

The x86-64 psABI process-initialization rules define, at process entry:

- `RSP` points to the lowest-address byte of the initial stack state
- `RSP` is 16-byte aligned
- `argc` is an eightbyte at `RSP`
- `argv[0..argc-1]` follows as eightbyte pointers
- `argv[argc] == 0`
- environment pointers follow and terminate with a null pointer
- the auxiliary vector follows and ends with an `AT_NULL` entry
- argument/environment strings and auxiliary information live in a higher-address information block; their internal order is not ABI-fixed

The generic ELF gABI defines `PT_TLS` as the thread-local storage template:

- `p_offset`: file offset of the TLS initialization image
- `p_vaddr`: virtual address of the TLS initialization image
- `p_filesz`: initialization-image byte count
- `p_memsz`: total TLS template byte count
- `p_align`: TLS-template alignment
- bytes in the template after `p_filesz` and before `p_memsz` are zero-initialized for each thread

The x86-64 psABI documents `%fs` as the thread-pointer register. Selecting the actual TCB/thread-pointer layout is **not** part of M1.

Primary references:

- https://gitlab.com/x86-psABIs/x86-64-ABI
- https://gabi.xinuos.com/elf.pdf

## 3. Design boundary

M1 produces **data descriptions and owned byte images**.

It does not:

- map writable host memory
- set CPU registers
- assign `FS.base`
- create a TCB
- execute guest instructions
- synthesize Linux-specific process state
- synthesize undocumented PS5-specific process state

Those belong to M2/platform research.

---

# Part A — Synthetic initial stack

## 4. Inputs

Conceptually:

```text
InitialStackRequest
    storage: GuestRange
    arguments: ordered strings
    environment: ordered strings
    auxiliary_vector: ordered (type,value) pairs
```

`storage` is a caller-supplied guest-address range reserved for the initial stack image.

The builder must not allocate a host buffer equal to the whole storage range merely because the guest capacity is large.

## 5. Output

Conceptually:

```text
InitialStackImage
    storage: GuestRange
    used_range: GuestRange
    rsp: GuestAddress
    bytes: owned byte vector
```

`bytes[0]` corresponds exactly to `used_range.base()`.

The byte vector covers only the used stack region, including deterministic alignment padding and the information block. It does not cover unused lower-address capacity.

The output owns its bytes and therefore cannot dangle if request strings are temporary.

## 6. Stack growth and placement

The synthetic stack grows toward lower addresses.

Astraea places the information block at the highest addresses available in `storage`, then places the pointer/control block below it.

The resulting `rsp` is the lowest address of the used image and must satisfy:

```text
rsp % 16 == 0
```

All layout arithmetic is performed as checked offsets inside `GuestRange`; code must not form a conceptual exclusive-end guest address if that would require representing `2^64`.

This permits a storage range whose final included byte is `UINT64_MAX`.

## 7. Deterministic information-block order

The ABI does not require an internal order for strings.

Astraea chooses this deterministic M1 order:

1. argument strings in input order
2. environment strings in input order

Each string is copied exactly once and receives one trailing NUL byte.

The information block is packed contiguously in that order at the high-address end of the used stack.

No host environment data is imported implicitly.

## 8. Input string rules

Inputs are byte strings represented by `std::string` or an equivalent owned/borrowed byte-string type.

For M1:

- empty strings are allowed
- embedded NUL bytes are rejected
- the builder appends exactly one terminating NUL
- no locale or UTF-8 validation is performed
- byte content is otherwise opaque

Rejecting embedded NUL avoids creating pointers to strings whose ABI-visible value is shorter than the supplied input.

## 9. Pointer/control block

At `rsp`, Astraea emits eightbyte values in little-endian order:

```text
argc
argv[0]
...
argv[argc-1]
0
envp[0]
...
envp[n-1]
0
auxv[0].type
auxv[0].value
...
AT_NULL
0
```

Every non-null argv/envp pointer is a **guest address** pointing into the owned information block.

No host pointer is serialized.

`argc` equals the number of argument strings.

`argc == 0` is valid and is followed immediately by the argv null pointer.

## 10. Auxiliary-vector input

Conceptually:

```text
AuxiliaryVectorEntry
    type: uint64
    value: uint64
```

M1 treats the pair generically and does not assign platform semantics.

The caller supplies entries **excluding** `AT_NULL` (type 0).

If any caller entry has type 0, construction fails with `auxv_contains_terminator`.

Astraea appends exactly one terminal pair:

```text
type = 0
value = 0
```

The builder does not automatically add Linux-specific entries such as `AT_RANDOM`, `AT_EXECFN`, `AT_UID`, or `AT_PAGESZ`.

Later synthetic-probe helpers may construct explicit auxv profiles above this primitive.

## 11. Alignment padding

The pointer/control block must begin at a 16-byte-aligned guest address.

Astraea may insert zero-filled padding between the end of the control block and the information block to achieve this.

Padding is part of `used_range` and part of the returned byte vector.

No ABI-visible pointer targets the padding.

The exact padding rule is deterministic:

> choose the highest possible control-block start address below the packed information block that is 16-byte aligned and leaves the complete control block inside `storage`.

This minimizes used stack bytes while preserving deterministic placement.

## 12. Stack size arithmetic

All of the following use checked arithmetic:

- per-string `size + 1`
- total information-block bytes
- `argc * 8`
- environment pointer count * 8
- auxv count * 16
- control-block total
- alignment adjustment
- used stack size
- guest-address translation for string pointers

Construction fails before allocation when the layout cannot fit in `storage`.

## 13. Host-size boundary

A valid guest capacity may exceed host `size_t`.

The builder allocates only the actual used byte count.

Before allocation:

- used byte count must be representable as `size_t`
- used byte count must not exceed `std::vector<std::byte>::max_size()`
- allocation failure is reported as a typed error

The full guest storage capacity does not need to be representable as a host allocation size merely to serve as a bounds description.

## 14. Stack errors

Stable categories should include at least:

- `embedded_nul`
- `auxv_contains_terminator`
- `layout_size_overflow`
- `stack_too_small`
- `host_size_unrepresentable`
- `guest_address_overflow`
- `host_allocation_failure`

Errors should include the failing argument/environment/auxv index where applicable.

Human-readable strings are not the stable API.

## 15. Stack tests

At minimum:

- argc == 0
- one/multiple arguments
- empty argument string
- environment entries
- empty environment
- auxv entries + exactly one generated AT_NULL
- caller AT_NULL rejection
- embedded NUL rejection
- all serialized integers are little-endian
- all argv/envp pointers resolve into returned bytes
- argv order preserved
- env order preserved
- RSP always 16-byte aligned
- deterministic repeat construction
- insufficient storage
- exact-fit storage
- alignment padding required
- storage range ending at `UINT64_MAX`
- extremely large guest capacity does not cause capacity-sized allocation

---

# Part B — Generic PT_TLS template

## 16. Discovery

`PT_TLS` has program-header type value 7.

M1 accepts:

- zero `PT_TLS` entries -> no TLS template
- exactly one `PT_TLS` entry -> validate/build descriptor

Multiple `PT_TLS` entries fail with `duplicate_tls_segment`.

Astraea uses validated `ProgramHeader` values; no section-header dependency is introduced.

## 17. TLS descriptor

Conceptually:

```text
TlsTemplateDescriptor
    source_program_header_index
    file_offset
    initialization_address: GuestAddress
    initialized_size: GuestSize
    total_size: GuestSize
    alignment: uint64
```

`p_vaddr` is preserved as a guest address.

It is never cast to a host pointer.

## 18. TLS size validation

Require:

```text
p_filesz <= p_memsz
```

Otherwise fail with `tls_initialized_size_exceeds_total`.

`p_memsz == 0` is valid.

`p_filesz == 0` is valid.

## 19. TLS file-range validation

When `p_filesz > 0`:

- `p_offset + p_filesz` uses checked arithmetic
- the complete half-open range must be contained in the original ELF byte span

When `p_filesz == 0`, no file bytes are read.

The raw `p_offset` is still preserved in the descriptor; M1 does not reject an otherwise-unused offset solely because the initialization image is empty.

## 20. TLS flags and alignment

For the generic `PT_TLS` profile, require:

```text
p_flags == PF_R
```

Any other flag set fails with `invalid_tls_flags`.

Interpret `p_align` as:

- 0 or 1 -> no special alignment requirement
- any value > 1 -> must be a power of two

Otherwise fail with `invalid_tls_alignment`.

When `p_align > 1`, also require the generic program-header congruence:

```text
p_vaddr % p_align == p_offset % p_align
```

A mismatch fails with `invalid_tls_alignment_congruence`.

The descriptor preserves the original alignment value.

M1 does not choose a guest TLS block address, so it does not yet perform runtime placement/alignment beyond validating the ELF metadata.

## 21. TLS template materialization

Conceptually:

```text
materialize_tls_template(descriptor, image_bytes)
    -> owned vector<byte>
```

The result contains exactly `total_size` bytes:

- bytes `[0, initialized_size)` copied from
  `image_bytes[file_offset .. file_offset + initialized_size)`
- bytes `[initialized_size, total_size)` equal zero

No guest virtual memory is allocated.

The materialized vector represents one generic TLS template/instance content image only.

## 22. TLS allocation safety

Before materialization:

- `total_size` must be representable as host `size_t`
- it must not exceed vector max size
- file source bounds are revalidated defensively
- allocation failure produces a typed error

A descriptor may exist even when its total size cannot be materialized on the current host, provided descriptor construction itself does not allocate that size.

## 23. TLS errors

Stable categories should include at least:

- `duplicate_tls_segment`
- `tls_initialized_size_exceeds_total`
- `tls_file_range_overflow`
- `tls_file_range_out_of_bounds`
- `invalid_tls_flags`
- `invalid_tls_alignment`
- `invalid_tls_alignment_congruence`
- `host_size_unrepresentable`
- `host_allocation_failure`

Errors should preserve the source program-header index and relevant file offset/size.

## 24. TLS tests

At minimum:

- no PT_TLS
- one valid initialized-only template
- initialized + zero-fill template
- all-zero template
- zero-size template
- duplicate PT_TLS
- filesz > memsz
- file-range overflow
- file range past EOF
- PF_R accepted
- non-PF_R flags rejected
- p_align = 0
- p_align = 1
- power-of-two alignments
- non-power-of-two alignment rejection
- aligned p_vaddr/p_offset congruence
- alignment-congruence rejection
- template bytes match source
- zero tail is actually zero
- descriptor preserves p_vaddr at `UINT64_MAX`
- huge `p_memsz` does not allocate during descriptor construction

---

# Part C — Runtime boundary

## 25. x86-64 register state

The x86-64 psABI also specifies selected initial register state.

M1 intentionally does not construct that register context.

In particular:

- `RSP` value is produced by the stack builder but installed only by M2
- `RDX` startup semantics are not synthesized here
- floating-point control/status setup is M2 execution-state work
- `FS.base` / thread-pointer installation is M2/platform work

## 26. Thread pointer / TCB

The x86-64 psABI uses `%fs` as the thread pointer.

That fact does **not** determine a PS5-compatible:

- TCB layout
- TLS variant/layout across modules
- static TLS allocation order
- DTV
- loader module IDs
- thread creation policy

M1 therefore stops at the generic TLS template.

A future provenance-backed platform layer may compose template metadata into a PS5-compatible per-thread layout.

## 27. PS5 boundary

The following remain explicitly unknown unless #12/probes establish them:

- exact retail PS5 initial stack contents
- PS5 auxv entries
- initial non-RSP register values
- stack randomization/guard layout
- TCB layout
- FS-base semantics beyond generic x86-64 thread-pointer usage
- main executable versus SPRX TLS module allocation
- TLS module IDs/offsets

Synthetic M1 tests must not be presented as hardware observations.

## 28. Implementation sequence

After acceptance:

1. **#49** — implement deterministic synthetic stack builder
2. **#50** — implement PT_TLS descriptor/materializer independently
3. **#51** — compose both into the final validated synthetic `GuestImage`
4. close M1 #2 only after the integrated synthetic exit fixture passes all gates

# 29. Decision summary

For M1:

> Astraea builds a deterministic generic x86-64 initial stack for controlled synthetic executables and a neutral generic ELF PT_TLS template. Both operate entirely on guest-address values and owned host byte images. CPU-register installation, TCB/FS-base layout, and PS5-specific bootstrap semantics remain later provenance-backed work.
