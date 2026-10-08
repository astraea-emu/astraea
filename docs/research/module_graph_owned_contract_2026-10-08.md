# Owned module-graph contract (C1 software-first, stage D)

**Status:** exploratory until PR #361's complete CI gate passes.

## Purpose and evidence

PRs #358–#360 classified the public native title's needed modules, opaque
SCE dynamic symbols, and relocation-to-symbol references without executing
guest code. A static identity is not sufficient to bind or call a system
service. This stage provides a deliberately minimal, generic **data-only**
module dependency graph that can be verified using *independently authored*
modules on PC hosts.

The graph accepts exact **caller-supplied** module keys, each declared
dependency key, and (where known) exported `SceSymbolIdentity` to typed
**guest addresses**. The graph **does not derive** any module key from
the raw SCE packed `DT_SCE_NEEDED_MODULE` records, guess IDs, bind NIDs to
Sony functions, perform HLE/LLE dispatch, apply relocations, or construct
initial PS5 process state. Caller-provided keys remain opaque until an
independent identity-mapping contract is evidenced.

## Structural and resolution rules

- Maximum 64 module declarations; per declaration maximum 64 dependencies
  and 512 exports; each key limited to 256 bytes.
- Duplicate module identities, duplicate dependency edges, invalid opaque
  keys, duplicate exported `nid/library_id/module_id` triplets, invalid
  export names and zero guest addresses fail graph creation.
- Resolution is exact and restricted to a requester-declared provider edge.
  Cross-provider fallback and global symbol scans are intentionally absent.
- A missing requester, undeclared dependency, unavailable provider,
  invalid symbol identity, or missing export yields a distinct typed
  **failure without a guest address**.
- Different explicit providers may export identical symbol triplets without
  conflict: the requesting module must select which provider to target.
- All returned addresses are typed guest virtual addresses and are **never**
  dereferenced or invoked by this graph. Import strong/weak binding policy,
  version matching, provider lifetime and relocation application require
  later separately evidenced work.

## Tests and limits

`tests/execution/module_graph_tests.cpp` uses owned, dummy modules and
opaque identities; tests exact resolution, unrelated providers, missing
dependencies, unregistered providers, duplicate edges/modules/exports and
invalid export addresses/identities. The tests make no claim about an
actual PS5 dynamic linker and are not evidence for firmware 13.00.

Before using this abstraction with commercial executable inputs, the
following must be independently demonstrated: mapping SCE long-form
symbol module/library IDs to a particular provider module identity,
relocation types and addends, weak/strong binding, mapping lifetime,
and the first guest-visible control transfer. Unknown fields remain
explicitly unsupported; no fake-success HLE or synthetic startup ABI
is permitted on the production retail path.

**Related:** #300, #334 (hardware observations), #348 (selected-process ABI),
#350 (first retail divergence), #356 (software-first dependency inventory).

## Stage E: exact symbol-to-provider association (still data-only)

`ModuleGraphImportPlan` joins one already-validated dynamic relocation and
one exact-index `SceDynamicSymbolRecord` to a caller-specified graph
requester/provider edge. It requires a defined-looking SCE long-form identity
for the undefined dynamic symbol and returns only a **planning value**
containing the original relocation target, raw type/addend, symbol index,
ELF binding nibble, provider identity, and typed guest address.

The planner refuses mismatched symbol indices, symbols defined in the caller's
ELF, absent long-form SCE identity, and each graph-resolution failure. It
does not infer which provider a PS5 import names, apply **any** relocation,
dereference the returned address, determine symbol-version compatibility,
treat a weak import as a zero value, or approve an unknown relocation type.
Weak/strong binding differences are retained as input metadata; determining
their runtime semantics requires separate evidence and tests.

The acceptance tests are **owned modules only** and do not exercise a
commercial PS5 title or solve the hardware ABI gate #334. They prove the
composition of existing checked loaders, exact symbol identities and a
fail-closed independent module graph without adding unverified Sony logic.

## Stage F: standard x86-64 owned absolute import encoding

For independently authored modules, `build_owned_x86_64_module_import_patch`
turns an already accepted stage-E provider association into eight deterministic
little-endian bytes. It accepts only:

- `R_X86_64_GLOB_DAT` (6) in a general RELA table: 64-bit `S`;
- `R_X86_64_JUMP_SLOT` (7) in a PLT RELA table: 64-bit `S`.

Here `S` is the exactly selected provider's **guest virtual address**.
Both types ignore the RELA addend when computing the value; the raw addend is
retained for diagnostics. The implementation follows the published AMD64
System V psABI, not an inferred Sony runtime-loader contract:
https://gitlab.com/x86-psABIs/x86-64-ABI

All other types and unsupported table encodings fail explicitly. The caller
must supply a RELA addend, an initialized nonzero export address, a generic ELF
global (1) or weak (2) symbol binding, and a non-overflowing eight-byte
relocation target range. Weak import **fallback behavior** is not inferred;
only an already resolved, explicitly selected weak symbol is acceptable. The output contains
**bytes only**. It does not modify guest memory, check mapping ownership or
write permissions, invoke HLE, establish provider lifetime, derive a Sony
module ID, or admit a commercial guest title.

Do not call this a PS5 relocation engine. Future integration requires a
separate audited memory-application boundary using exact mapped/owned address
ranges and conflict/rollback policies, with observed relocation types from
the selected lawful workload and evidence for any PS5-specific differences.
