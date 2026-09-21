# Initialized Guest-Image Byte View — M1

**Status:** Proposed  
**Issue:** #27  
**Scope:** deterministic, host-independent reads of initialized guest-image bytes

## 1. Purpose

Later M1 stages need to read bytes that a validated ELF image would place into the guest address space:

- dynamic strings
- dynamic symbols
- relocation tables
- future bootstrap metadata

Those consumers must not reinterpret guest virtual addresses as host pointers or bypass the validated mapping model.

This specification defines a read-only initialized-image view built from:

1. the original bounded ELF byte buffer
2. validated `MappingIntent` records derived from `PT_LOAD`

It does **not** define writable runtime guest memory.

## 2. Core rule

A guest address identifies a byte in the guest address space, not a byte in the host process.

Resolving a guest byte requires an explicit mapping step:

```text
GuestAddress
    ↓
mapping intent(s) covering that address
    ↓
deterministic initialized-byte source
    ↓
file byte OR synthesized zero
```

No cast from guest integer address to pointer is permitted.

## 3. View lifetime and ownership

The initialized-image view may borrow the original ELF bytes through a bounded `std::span<const std::byte>` or an equivalent non-owning view.

If it borrows:
- the caller owns the backing bytes
- the view must not outlive them
- returned byte/string results must not create dangling references

M1 should prefer APIs that return copied bytes/owned strings to higher layers unless a lifetime-safe bounded view is straightforward and explicit.

The view does not own or allocate the guest address space.

## 4. Inputs

A view is constructed from:

- `std::span<const std::byte> image_bytes`
- a deterministic sequence of validated `MappingIntent` records

The mapping sequence may contain:
- file-backed initialized bytes
- zero-fill initialized bytes
- future anonymous mappings

For M1 guest-image reads, anonymous/runtime-only mappings do not provide initialized image content unless a future policy explicitly says otherwise.

## 5. Construction-time validation

Before any read is allowed, the view validates every input intent that can expose initialized bytes.

### 5.1 Guest ranges

Each intent's guest range is already expected to be valid, but the view must not rely on unchecked external construction.

Any internal transformation must preserve full-domain range semantics, including a valid one-byte range at `UINT64_MAX`.

### 5.2 File-backed intents

For every file-backed intent:

- `backing.byte_count` must equal the guest range size for M1
- `file_offset + byte_count` must be checked for overflow
- the complete file range must be contained within `image_bytes`
- no host pointer/view may be created before those checks succeed

A file-backed intent whose backing range is invalid makes view construction fail.

### 5.3 Zero-fill intents

For zero-fill:

- `backing.byte_count` must equal the guest range size
- no file offset is interpreted
- reads synthesize `std::byte{0}`

Zero-fill must not allocate an array equal to the guest range size.

### 5.4 Unsupported backing kinds

For M1, any backing kind other than `file` or `zero_fill` makes view construction fail with `unsupported_backing_kind`.

In particular, `anonymous` is a reserved runtime-memory concept and is not initialized guest-image content. It must never silently yield zero.

A future ADR may add another initialized-image backing kind, but M1 does not carry partially readable views.

## 6. Byte-source identity

The initialized-byte identity for a guest address is defined independently of permissions.

For a covering mapping intent:

### File-backed

```text
source_file_offset =
    intent.file_offset
    + (guest_address - intent.guest_base)
```

The subtraction/addition must be overflow-safe. Construction validation guarantees the derived position lies in the original image.

### Zero-fill

The source identity is simply `zero-fill`.

Permissions are not part of byte-source identity. Two mappings may request different guest permissions yet still describe the same initialized byte content.

## 7. Overlap resolution

A guest address may be covered by multiple intents.

The view must examine **all** covering initialized intents before returning a byte.

### 7.1 Compatible file-backed overlap

Multiple file-backed intents are compatible for a guest address only when each resolves that address to the exact same source file offset.

Example:

```text
intent A: guest 0x1000 -> file 0x200
intent B: guest 0x1080 -> file 0x280

guest 0x1080:
A -> file 0x280
B -> file 0x280
compatible
```

### 7.2 Compatible zero-fill overlap

Any number of zero-fill intents covering the same address are content-compatible.

### 7.3 File versus zero-fill

File-backed plus zero-fill coverage is a conflict even if the current file byte happens to equal zero.

Reason: compatibility is based on source semantics, not accidental value equality.

### 7.4 Different file sources

Two file-backed mappings resolving the same guest address to different file offsets are a conflict even if the bytes currently compare equal.

### 7.5 Permissions

Permission disagreement does not make byte content ambiguous.

Permission reconciliation belongs to executable/host mapping policy. The initialized-byte view answers only: **what initialized byte does this guest address represent?**

### 7.6 No implicit winner

Intent order, source index, container iteration order, or "first match" must never decide a conflicting overlap.

Conflicts produce a typed error.

## 8. Single-byte read

The fundamental operation is conceptually:

```text
read_byte(GuestAddress) -> Result<std::byte, GuestImageReadError>
```

Possible outcomes:

- exactly one deterministic initialized source -> return byte
- no initialized mapping -> `unmapped_guest_address`
- incompatible overlap -> `mapping_overlap_conflict`

Because unsupported backing kinds are rejected during construction, a successfully constructed M1 view contains only readable initialized sources.

The error should include the queried guest address and relevant stable source indices where practical.

## 9. Range reads

Consumers should not need to manually loop over raw mappings.

A bounded range-read operation should support a requested `GuestRange` that may cross adjacent compatible intents.

Conceptually:

```text
copy_initialized_bytes(GuestRange, output_span)
```

Requirements:

- before comparing/allocating host-sized output, the guest byte count must be proven representable as `size_t`
- an otherwise valid guest range that cannot be represented by the host buffer API fails with `host_size_unrepresentable`
- output size must exactly match the representable guest range size for this API shape
- empty range succeeds without reading
- each byte is resolved according to the same overlap rules as `read_byte`
- the requested range may cross file-backed to file-backed boundaries
- the requested range may cross file-backed to zero-fill boundaries
- no implicit requirement that the source be one contiguous file slice
- failure identifies the first guest address that could not be deterministically resolved

The implementation may optimize contiguous runs after correctness is established.

## 10. File-backed span optimization

The view may expose an optimization such as "contiguous file-backed span for this guest subrange" only when all bytes map monotonically to one contiguous source file range.

This is an optimization, not the semantic API.

Higher layers such as dynamic string resolution must still work when a valid string crosses compatible mapping boundaries.

## 11. Determinism

Given identical:

- original image bytes
- mapping intents

the view must return identical bytes/errors regardless of:

- host OS
- host architecture
- memory allocation addresses
- container/hash iteration order

Mapping intents used internally should be ordered deterministically or queried in a way whose result does not depend on iteration order.

## 12. Error model

Initial stable categories should include at least:

- `invalid_file_backing_range`
- `backing_size_mismatch`
- `unsupported_backing_kind`
- `unmapped_guest_address`
- `mapping_overlap_conflict`
- `output_size_mismatch`
- `host_size_unrepresentable`
- `guest_range_overflow` where internal construction requires it

Errors should preserve:
- guest address of failure where applicable
- first/second conflicting source indices where applicable
- file offset for invalid file-backed ranges where useful

Human-readable text is not the stable API.

## 13. Complexity

Correctness comes first, but M1 should avoid pathological behavior.

A naïve full scan of every mapping intent per byte is acceptable only as a temporary implementation if fixtures are tiny.

Preferred direction:
- deterministic sort by guest base/source index
- identify candidate mappings intersecting the requested range
- resolve contiguous runs where possible

Do not introduce a complex interval tree before measurements justify it.

## 14. Security properties

- no unchecked guest/file arithmetic
- no read outside the original image span
- no guest integer-to-pointer cast
- no unbounded allocation based on guest range size
- no source conflict hidden by insertion order
- no mutation of image bytes or guest state
- no lifetime-unsafe returned view

## 15. Testing matrix

### Construction

- valid file-backed mapping
- exact file-end boundary
- file-range overflow
- file range beyond image
- file backing byte-count mismatch
- zero-fill byte-count mismatch
- unsupported backing kind

### Single-byte reads

- first/last byte of file-backed range
- first/last byte of zero-fill range
- unmapped address
- final guest address `UINT64_MAX`
- compatible same-file overlap
- compatible zero-fill overlap
- file/zero-fill conflict
- different-file-offset conflict
- differing permissions with same source remain readable

### Range reads

- empty range
- one file-backed range
- file-backed range ending exactly at image end
- cross two adjacent file-backed intents
- cross file-backed -> zero-fill
- cross zero-fill -> file-backed
- first failure address is stable
- full-domain range ending conceptually at `2^64`
- output size mismatch
- guest range whose size cannot be represented by host `size_t` (where testable)

## 16. Relationship to dynamic strings

Issue #29 will use this view to resolve a `DynamicStringRef`.

That layer will:
- validate offset against `DT_STRSZ`
- derive a bounded guest subrange within the string table
- read bytes through this contract
- find a NUL terminator before table end
- return a lifetime-safe resolved string

Dynamic string code must not directly inspect `MappingIntent` or perform file-offset translation itself.

## 17. Out of scope

This view is not:
- runtime guest RAM
- writable memory
- host virtual-memory allocation
- relocation application
- page protection
- native execution
- PS5 syscall memory semantics
- an executable-memory cache

Those belong to later milestones/backends.

## 18. Decision summary

For M1:

> Initialized guest-image bytes are resolved through validated mapping intents. Overlapping sources are compatible only when they identify the same file byte or are all zero-fill. Permissions do not alter byte identity. Conflicting source ownership is an error, never a first-match policy.

This gives later dynamic strings, symbols, and relocations one deterministic read boundary that remains portable on macOS ARM64, Linux x64, and Windows x64.
