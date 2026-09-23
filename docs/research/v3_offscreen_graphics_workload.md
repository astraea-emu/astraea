# V3 owned offscreen graphics workload

**Decision date:** 2026-09-23  
**Issue:** #172  
**Gate:** V3 — submitted guest GPU state/resources -> Vulkan -> deterministic result

## Decision

The first **raster** V3 target is an **Astraea-owned offscreen graphics
workload** that eventually renders a deterministic uniform color into one tiny
color target and reads the logical result back through Vulkan.

Before that raster path is expanded further, #175 inserts a smaller bounded
WRITE_DATA guest-buffer micro-gate to establish the reusable guest GPU
address/resource substrate independently. That micro-gate does not replace this
workload or complete V3.

The target is intentionally small:

- 4x4 color attachment;
- one primitive sufficient to cover the target;
- one owned vertex-stage program and one owned pixel-stage program;
- deterministic constant/uniform output;
- one color target;
- no depth/stencil;
- no blending;
- no multisampling;
- no texture or sampler;
- no VideoOut, flip, presentation, or frame pacing;
- no third-party shader binary in the repository.

The final proof is successful only when every guest-domain dependency on that
path is represented explicitly and the Vulkan result is deterministic.

## Why this is the next workload

Astraea has already connected:

```text
owned SCE guest
    -> sceAgcCreateShader
    -> persistent created pixel shader
    -> captured SubmitDcb
    -> PM4 Type-3 framing
    -> SET_SH_REG state
    -> pixel PGM_LO/PGM_HI GPU address
    -> unique created-shader binding
```

The next useful question is no longer whether the submitted DCB selects the
right shader. It is what minimum additional guest graphics state/resources are
required before that selected shader can participate in a real host-GPU
workload.

The offscreen workload gives that question a concrete finish line without
pulling presentation into V3.

## Evidence for the graphics path

### Hardware-oriented public PS5 sample

Pinned source:

- Rufidj/ps5link-sdk
- commit `ea771e535378740b6a058b8e5419eb8a0e0e0ec8`
- `examples/gpu_cube/main.c`

The public sample performs the following observable high-level sequence:

1. load an AGC vertex program and pixel program;
2. call `sceAgcCreateShader` for both;
3. establish render-target and viewport/context state;
4. link the vertex/pixel programs;
5. record context-register and shader-register state;
6. bind vertex-stage resources;
7. establish index/draw state;
8. issue a draw;
9. submit the DCB with `flag = 0`.

Its comments state that the pixel program uses no resources while the vertex
stage owns the constant/vertex buffers.

This is not copied into Astraea. It establishes that the broad dependency
shape is exercised by public PS5 homebrew.

### Independent public AGC model

Pinned source:

- SvenGDK/SharpProspero
- commit `9220876e25bc28aca1f65ea644783a479949ad77`
- `docs/graphics-gpu.md`
- `src/SharpProspero/Interop/Agc/SceAgc.cs`

The public model independently separates:

- command-buffer register writes;
- shader creation;
- shader linking;
- render-target/context blocks;
- shader user-data/resource descriptors;
- draw and dispatch builders;
- driver submission.

It also exposes a compute shader kind and dispatch builders. That confirms
compute is a real architectural path, but it does not provide an owned
hardware-proven compute shader/container + dispatch example comparable to the
graphics path reviewed for this decision.

## Why graphics instead of compute

Compute could eventually be a cleaner backend proof because it avoids
rasterization. It is not selected now because doing so would first require
establishing several compute-specific PS5 contracts for which the current
public evidence is thinner:

- evidence-backed compute AGC shader-object preparation;
- compute program-register identity;
- a controlled/public compute shader container;
- dispatch-state semantics;
- writable guest resource semantics.

By contrast, Astraea has already implemented the pixel create/bind path and
current public PS5 examples exercise graphics creation, linkage, context
state, draw, and submit.

The workload choice is therefore evidence-driven, not a claim that graphics
is intrinsically simpler than compute.

### Revisit condition

Reconsider compute if either:

- stronger public/controlled compute evidence appears; or
- this graphics workload reaches a PS5-specific blocker for which a compute
  path has substantially stronger evidence and a shorter verified dependency
  chain.

## Why offscreen instead of presentation

V3 is defined around:

```text
guest GPU state/resources -> Vulkan -> deterministic result
```

Presentation adds independent behavior:

- VideoOut buffer registration;
- flip packets/calls;
- synchronization with display ownership;
- frame pacing and presentation state.

Those belong later in the V5 presentation path.

The V3 workload therefore stops at deterministic offscreen Vulkan readback.

## Final workload contract

The finished workload should conceptually be:

```text
owned guest shader/resource setup
    -> owned submitted DCB
    -> complete typed guest state required by this workload
    -> selected owned vertex + pixel shaders
    -> resolved owned color-target resource
    -> semantic Shader IR
    -> compiler/value IR when demanded by the workload
    -> validated SPIR-V
    -> Vulkan graphics pipeline
    -> 4x4 offscreen image
    -> deterministic logical RGBA result
```

The expected result is a single uniform color over the complete 4x4 target.
The exact color is chosen only when the owned shader instruction sequence is
specified; this document deliberately does not invent an ISA sequence first
and rationalize it afterward.

## Dependency frontier

The public graphics sequence and current Astraea state imply at least these
future dependency classes:

1. generic submitted context-register state;
2. PS5-specific meaning for only the context registers required by this target;
3. non-pixel shader creation/identity required by the owned primitive path;
4. shader linkage / primitive state required by that path;
5. draw/index packet semantics;
6. any minimal vertex input/resource descriptor semantics actually required;
7. vertex position export semantics;
8. pixel color export semantics;
9. guest color-target identity/layout semantics;
10. Vulkan graphics-pipeline/image realization and deterministic readback.

This is a dependency list, **not** an implementation order frozen in advance.
After each bounded slice, the workload is re-evaluated and the next first
missing dependency is selected.

## Raster dependency sequence

The first raster-specific dependency selected from this workload was **generic
context-register transport/state** (#173), and that bounded slice is now being
implemented independently.

The project then inserts #175's WRITE_DATA W0-W2 resource micro-gate before
assigning PS5-specific context-register meanings, because guest GPU
address/resource resolution is reusable by the eventual vertex buffers and
color target and can be proven with fewer simultaneous raster unknowns.

### First raster-specific dependency

Astraea already has a typed/persistent shader-register path, but Graphics IR
has no context-register write operation and there is no persistent
`ContextRegisterState`.

Generic AMD public evidence identifies:

- Type-3 `SET_CONTEXT_REG` opcode `0x69`;
- context register range `0xA000..0xA400` (0x400 relative dwords);
- the ordinary relative-offset + consecutive-value packet envelope.

Relevant public AMD sources include Linux AMDGPU PM4 definitions and Mesa's
AMD command-buffer helpers.

That generic transport/state boundary must exist before Astraea can safely
assign narrow PS5 meanings to the render-target/viewport fields the workload
will eventually require.

Issue #173 owns that next implementation slice.

## #173 boundary

The next code step is only:

```text
PM4 Type-3 SET_CONTEXT_REG
    -> GraphicsIrContextRegisterWriteRange
    -> ContextRegisterState
```

It must:

- preserve raw packet provenance;
- distinguish uninitialized state from explicitly written zero;
- validate complete ranges before mutation;
- reject unsupported control/index bits;
- keep shader-register and context-register domains separate.

It must **not**:

- label offsets as render-target, viewport, scissor, blend, or depth state;
- decode PS5-specific context-register fields;
- implement draw packets;
- resolve resources;
- call Vulkan;
- return fake SubmitDcb success.

## Open questions intentionally deferred

The following remain evidence/dependency questions, not assumptions:

- the smallest owned vertex-stage instruction sequence;
- the exact stage ABI for the owned primitive;
- the exact shader-linkage state needed by the target;
- the final draw packet/profile;
- which render-target fields are required when all disabled/default features
  are stripped away;
- the smallest evidence-backed guest color-target layout;
- whether logical host readback is sufficient for the first V3 completion or
  whether a later gate must also reproduce guest tiled memory bytes;
- the point at which ADR 0007's compiler/value IR becomes necessary.

## Clean-room rule

Third-party public projects are used only to establish observable architecture
and evidence questions.

Astraea will not copy their implementation code, shader binaries, proprietary
Sony SDK material, firmware, keys, retail assets, or undocumented constants
solely because they appear in a third-party implementation.

PS5-specific semantics remain evidence-gated under `docs/CLEAN_ROOM.md`.
