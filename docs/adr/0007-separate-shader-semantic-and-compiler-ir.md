# ADR 0007: Separate shader semantic IR from compiler/value IR

**Status:** Accepted  
**Date:** 2026-09-22

## Context

Astraea's existing Shader IR was deliberately designed as a guest-semantic
boundary for AMD-documented RDNA2 behavior. It represents concepts such as
SGPR/VGPR operands, SCC/VCC/EXEC-dependent control flow, wait counts, barriers,
and exact instruction provenance. The wave interpreter and Trace v0 consume
that representation as a correctness oracle.

Issues #150/#151 then proved a deliberately narrow direct lowering from
straight-line vector Shader IR (NOP/V_MOV_B32/V_ADD_F32/END) to deterministic,
Vulkan-valid SPIR-V 1.6. Issues #152/#153 execute that module headlessly on
Vulkan in mandatory Linux CI and compare the result bit-for-bit with the
semantic interpreter.

That proof establishes the backend path; it does not imply that a
register-machine semantic IR should also become Astraea's only production
compiler representation.

SPIR-V intermediate values use static single assignment (SSA), functions are
control-flow graphs, and Vulkan shader control flow is subject to structured
control-flow rules. Future PS5 workloads will also require stage I/O, resource
operations, memory semantics, exports, divergent control flow, and compiler
analysis that are different concerns from faithfully representing guest
register-machine semantics.

Astraea needs to preserve the semantic/oracle value of the current Shader IR
without forcing every future host-shader transformation directly into it.

## Decision

Astraea separates **shader semantics** from **shader compilation**.

### Semantic Shader IR remains the guest-domain oracle

The existing Shader IR remains responsible for:

- AMD/RDNA2 guest instruction semantics;
- SGPR/VGPR and special-register meaning;
- SCC/VCC/EXEC-sensitive behavior;
- waits/barriers as guest-semantic operations;
- instruction/source-offset/raw-encoding provenance;
- exact interpreter/oracle execution where useful;
- stable trace/differential behavior.

It remains backend-independent and must not contain Vulkan handles,
VkDeviceAddress values, descriptor-set numbers chosen only for the host
backend, or other host API objects.

The semantic interpreter continues to operate on this layer.

### Compiler/value IR is a separate, workload-driven layer

When the first concrete owned workload requires capabilities that are awkward
or unsafe to express by direct semantic-IR -> SPIR-V lowering, Astraea will
introduce the **smallest compiler/value IR required by that workload** between
semantic Shader IR and host shader code.

Expected compiler-layer responsibilities include, only as demanded:

- explicit typed values suitable for SSA/use-def reasoning;
- basic blocks and compiler-oriented control flow;
- structurization/lowering required by the host shader environment;
- phi/value merging where needed;
- explicit stage inputs/outputs and exports;
- resource/memory operations after guest resource semantics are known;
- backend-independent optimization/lowering opportunities that are justified
  by a real workload;
- provenance mapping back to semantic Shader IR and ultimately RDNA2 source.

The compiler IR is not itself a new source of PS5 semantics. Guest behavior is
established before or while lowering into it.

### SPIR-V remains a backend IR

SPIR-V remains the first host shader IR because Vulkan consumes SPIR-V.

The compiler path is therefore conceptually:

```text
PS5 AGC shader
    -> bounded RDNA2
    -> semantic Shader IR + CFG
    -> optional workload-driven compiler/value IR
    -> SPIR-V
    -> Vulkan
```

"Optional" here means the existing narrow vector-probe direct emitter remains
valid. Astraea does not rewrite a proven path merely to satisfy a layering
diagram. New production compiler coverage moves through the compiler/value IR
once the workload that justifies that layer exists.

### No speculative compiler framework

This ADR does **not** authorize building a broad optimizer, generic shader
language, LLVM/MLIR integration, or full SSA framework ahead of a workload.

The first compiler-IR issue must identify:

- the owned workload that requires it;
- the exact semantic Shader IR operations that enter it;
- the exact SPIR-V/backend limitation being removed;
- required provenance;
- the minimum control-flow/value/resource model needed;
- explicit unsupported behavior.

## Consequences

### Positive

- The interpreter/oracle representation can remain close to guest machine
  semantics instead of being distorted by host compiler needs.
- SPIR-V lowering can gain value/SSA/structured-CFG machinery without making
  Vulkan concepts part of guest semantics.
- A future second shader backend can consume the compiler layer without
  redefining RDNA2 semantics.
- Provenance can remain explicit across both semantic and compiler
  transformations.
- Astraea can continue using the already-proven direct vector-probe backend
  until a real workload justifies more machinery.

### Negative / constraints

- A production shader may eventually pass through one additional IR boundary.
- Provenance must be maintained across that boundary.
- Some semantic operations may require non-trivial lowering before they have a
  convenient compiler representation.
- Two IRs create naming/ownership overhead, so the compiler IR must stay
  workload-driven rather than becoming an architecture project by itself.

## Alternatives considered

### Keep direct semantic Shader IR -> SPIR-V forever

Rejected as the long-term contract. It works for the current straight-line
probe, but it couples a guest register-machine representation directly to an
SSA/structured host IR and would accumulate backend-specific transformations
as control flow/resources/stage I/O grow.

### Convert the existing Shader IR into the compiler IR

Rejected. Astraea would lose a clear register-semantic/oracle boundary and
risk changing interpreter/trace meaning to satisfy backend compilation.

### Use SPIR-V itself as Astraea's only compiler IR

Rejected as the architectural boundary. SPIR-V is a host/backend contract with
its own structured-control-flow and environment rules; guest behavior should
not be defined by what is easiest to express directly in SPIR-V.

### Adopt LLVM IR, MLIR, or another large compiler framework now

Rejected for the current critical path. No owned Astraea workload has yet
demonstrated that dependency/complexity is required.

## Evidence / references

Primary specifications:

- Khronos SPIR-V 1.6 unified specification:
  https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
- Vulkan SPIR-V environment:
  https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
- AMD RDNA 2 ISA reference guide 70648:
  https://docs.amd.com/v/u/en-US/rdna2-shader-instruction-set-architecture

Astraea evidence:

- `include/astraea/graphics/shader_ir.hpp`
- `docs/research/spirv_vector_probe.md`
- `docs/research/vulkan_vector_probe_execution.md`
- #150/#151 and #152/#153
- ADR 0006

Comparative public emulator/compiler projects were reviewed only to test
whether this separation addresses known scaling pressures. They are
evidence/context, not implementation sources, under `docs/CLEAN_ROOM.md`.

## Revisit triggers

Revisit this decision if:

- a representative resource/control-flow workload shows that the semantic IR
  can map cleanly to every required backend without accumulating
  backend-specific semantics;
- the compiler/value IR duplicates guest semantics without simplifying
  compilation or verification;
- a non-SPIR-V backend materially changes the useful compiler boundary;
- a proven external compiler IR can replace the planned layer with lower
  complexity while preserving Astraea's provenance and clean-room contracts.
