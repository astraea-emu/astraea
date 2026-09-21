# Guest Virtual-Address and Mapping Model — M1

**Status:** Proposed  
**Milestone:** M1 — complete  
**Scope:** platform-neutral representation of guest addresses, ranges, permissions, and mapping intent

## 1. Purpose

Astraea must represent a PS5 guest address space without assuming that guest addresses are host pointers.

That distinction is mandatory for:
- Apple Silicon development
- future non-x86-64 execution backends
- safe parsing of untrusted executable metadata
- deterministic tracing
- testing mapping logic without reserving host virtual memory

This document defines the M1 data model only. Host allocation and native-execution mechanisms belong to later milestones.

## 2. Fundamental rule

A guest virtual address is a 64-bit **value**, not a C++ pointer.

Forbidden design patterns include:

- storing guest addresses as `void*`
- pointer arithmetic to validate guest ranges
- deriving guest identity from host allocation addresses
- assuming guest and host page sizes are equal
- requiring a one-to-one guest-to-host address mapping in portable code

Architecture-specific execution backends may translate a validated guest address into a host pointer internally, but that translation must remain behind an explicit interface.

## 3. Core value types

M1 should define narrow value types rather than passing raw integers everywhere.

Conceptually:

```cpp
GuestAddress   // uint64_t value
GuestSize      // uint64_t byte count
GuestRange     // base + size, with half-open semantics
GuestPerms     // R/W/X bit set
```

The exact C++ spelling is an implementation decision for the implementation issue, but the semantic contracts below are required.

## 4. GuestAddress

`GuestAddress`:

- stores exactly a 64-bit unsigned value
- has no implicit conversion to/from a pointer
- may support explicit conversion to its underlying integer for serialization/debugging
- supports ordering/equality
- supports only checked addition/subtraction with sizes/offsets
- does not wrap

No M1 code assumes a specific canonical-address width. Future platform research may add address-width validation as a separate policy layer.

## 5. GuestSize

`GuestSize` represents a byte count.

Requirements:

- 64-bit unsigned semantic domain
- checked add/subtract
- explicit host-`size_t` conversion only after representability is proven
- no multiplication without checked arithmetic

A guest size does not imply allocated host memory.

## 6. GuestRange

Ranges use half-open semantics:

```
[base, base + size)
```

but the implementation should store `base` and `size` (or an equivalently safe representation), not require the conceptual one-past-end boundary to fit in a `uint64_t`.

For a non-empty range, validity means the **highest included address** is representable:

```
base + (size - 1) <= UINT64_MAX
```

This permits `GuestRange{UINT64_MAX, 1}`. Its conceptual one-past-end boundary is `2^64`, which is useful mathematically even though it is not itself a guest address.

### 6.1 Empty range

A zero-size range is valid and contains no addresses. Its base is retained for deterministic metadata/debugging.

An empty range:
- contains no address
- does not overlap another range
- may be preserved for diagnostics but should not produce a host mapping

### 6.2 Containment

Containment must be implemented using overflow-aware base/size arithmetic, preferably subtraction-based comparisons, so it also works when the conceptual one-past-end boundary equals `2^64`.

For non-empty range `R`, address `x` is contained iff `x >= base` and `x - base < size`. This subtraction form avoids constructing an unrepresentable one-past-end boundary.

### 6.3 Overlap

Two non-empty ranges overlap when either range contains the other's first address. Implementations should use checked/subtraction-based comparisons rather than requiring both one-past-end boundaries to fit in 64 bits.

Adjacency is not overlap.

All range operations must be testable without host allocation.

## 7. Permissions

Guest permissions are represented explicitly as independent bits:

- Read
- Write
- Execute

No permission implies another permission.

For example:
- Execute does not implicitly mean Read
- Write does not implicitly mean Read

If a host OS requires stronger permissions than the guest semantics, the backend must document that widening rather than mutating the guest model.

Unknown/extra platform permissions should not be silently folded into RWX; future extensions can add separate metadata.

## 8. Mapping intent versus host mapping

M1 distinguishes two concepts.

### 8.1 Mapping intent

A mapping intent describes what the guest image requires:

- guest range
- guest permissions
- backing/source semantics
- provenance/index identifying the originating load record
- deterministic ordering information

It is pure data.

### 8.2 Host mapping

A host mapping is an execution-backend resource that satisfies one or more mapping intents using OS-specific APIs.

Host mappings are **out of scope for M1**.

This separation allows mapping-plan tests to run on macOS ARM64 even though later native guest execution is x86-64-specific.

## 9. Backing/source model

The initial model must be able to represent at least:

### File-backed bytes

A bounded slice of the validated input image:

- file offset
- byte count
- guest destination range

### Zero-fill bytes

A guest range whose initial value is zero.

This covers the ELF `PT_LOAD` rule where `p_memsz > p_filesz`.

### Anonymous bytes

Reserved for later process/HLE memory services.

M1 does not need shared/device-backed mappings, but the representation should be extensible without reinterpreting existing enum values.

## 10. ELF load-segment transformation

A validated ELF `PT_LOAD` record conceptually yields:

1. a file-backed intent for `p_filesz` bytes at `p_vaddr`
2. if `p_memsz > p_filesz`, a zero-fill intent immediately following it for the remaining bytes
3. the guest permissions derived from `PF_R/PF_W/PF_X`

The transformation must use checked arithmetic.

It must not allocate `p_memsz` bytes merely to create the plan.

## 11. Overlap policy

The raw intent model **permits overlaps**.

Reason: executable formats may describe segments whose byte ranges or page-granular host mappings interact. Rejecting all overlap at the structural layer would encode an unsupported loader assumption.

M1 therefore separates:

- detection
- classification
- resolution

### 11.1 Detection

The planning layer must deterministically identify every pair/set of overlapping non-empty guest ranges.

### 11.2 Classification

At minimum classify overlap as:

- identical backing + compatible permissions
- different backing
- conflicting initialized bytes
- file-backed versus zero-fill
- permission disagreement

### 11.3 Resolution

M1 does not invent PS5-specific overlap semantics.

For synthetic fixtures, contradictory byte ownership must return a typed plan error unless an explicit resolution policy exists.

Future platform evidence may define page-level behavior in a dedicated ADR.

## 12. Ordering and determinism

A mapping plan has stable ordering independent of:
- host allocation address
- hash-map iteration order
- thread scheduling

Recommended order:

1. guest base address
2. guest end address
3. original source/program-header index
4. backing kind

Equivalent inputs must produce byte-for-byte equivalent serialized plans once serialization exists.

## 13. Guest page granularity

M1 must **not hard-code a PS5 page size** without provenance.

Portable planning can operate on byte ranges.

When page-aware operations are required, page size/granularity must be supplied explicitly by the platform/execution policy and validated to be:
- non-zero
- a power of two where the policy requires it

Host page size and guest page size are separate concepts.

## 14. Address-space object boundaries

The M1 address-space model should support queries such as:

- is this range representable?
- what mapping intents overlap this range?
- does this address have a mapping intent?
- what guest permissions are requested here?
- which source record produced this region?

It should **not** expose:
- direct host pointers
- OS mapping handles
- executable host-memory mutation
- PS5 syscall semantics

## 15. Error model

Typed errors should include at least:

- `guest_range_overflow`
- `host_size_unrepresentable`
- `invalid_permission_bits`
- `invalid_backing_range`
- `mapping_overlap_conflict`
- `invalid_page_granularity`

Errors involving multiple intents should identify their stable source indices.

## 16. Testing matrix

### Address/range arithmetic

- base 0, size 0
- base 0, size 1
- maximum address with zero size
- `GuestRange{UINT64_MAX, 1}` is valid
- a range whose highest included address would exceed `UINT64_MAX` is invalid
- adjacent ranges
- partially overlapping ranges
- contained ranges
- identical ranges
- empty ranges at boundaries

### Permissions

- every RWX combination
- no implied permissions
- stable serialization/order representation

### ELF transformation

- `p_filesz == p_memsz`
- `p_memsz > p_filesz`
- `p_filesz == 0, p_memsz > 0`
- zero-sized load segment
- virtual-range overflow rejected before intent creation
- permission translation for all flag combinations

### Overlap

- adjacent load segments
- exact duplicate intent
- compatible overlap
- file-backed/zero-fill overlap
- conflicting file-backed sources
- differing permission requests

## 17. Security properties

- No range arithmetic wraps.
- No host pointer is created during planning.
- No host allocation size is derived without representability validation.
- Mapping-plan construction is bounded by validated input metadata.
- Query behavior is deterministic.
- Untrusted input cannot cause a mapping merely by being parsed.

## 18. Future execution-backend contract

A later execution backend may implement something conceptually equivalent to:

```text
guest address
    -> validated address-space lookup
    -> backend translation
    -> bounded host view / executable mapping
```

The backend owns:
- OS allocation
- host page protection
- x86-64 executable-memory policy
- fault handling
- guest/host translation caches

Portable M1 code owns none of those mechanisms.

## 19. Relationship to other issues

- The ELF64 specification defines structural parsing invariants.
- The loader parses ELF64 into validated records.
- This document defines address/mapping semantics.
- Guest-image construction uses the validated model when constructing initial stack/TLS state.
- The native x86-64 transition specification defines guest/host execution transitions.

## 20. Decision summary

For M1:

> Guest addresses are typed 64-bit values; guest ranges are checked half-open byte ranges; mapping intent is pure deterministic data; host mappings are an execution-backend concern; overlaps are detected and classified rather than guessed away; and no PS5 page-size assumption is permitted without evidence.

This model deliberately keeps the loader testable on Apple Silicon while preserving a clean path to native x86-64 execution later.
