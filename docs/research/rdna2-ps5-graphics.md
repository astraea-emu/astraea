# RDNA2 / PS5 graphics evidence map

**Issue:** #9  
**Last verified:** 2026-09-21  
**Purpose:** establish what Astraea may safely assume about the PlayStation 5
graphics path before packet, shader, IR, or Vulkan implementation choices
harden.

## Scope

This note maps public evidence for:

- the PS5 GPU hardware baseline;
- generic RDNA2 shader-ISA semantics;
- command/state representation observed in public PS5-oriented work;
- shader container and resource-description questions;
- the boundary between a guest graphics frontend, Graphics IR, Shader IR,
  SPIR-V, and a Vulkan host backend;
- the experiments needed to falsify assumptions before compatibility code is
  written.

It does **not**:

- use proprietary Sony SDK documentation;
- embed firmware, keys, retail shaders, games, or proprietary system modules;
- describe DRM/authentication bypass;
- copy another emulator's GPU implementation;
- assume that desktop Radeon RDNA2 behavior is automatically PS5 behavior;
- assume that a public community project's interpretation is platform law.

The implementation rule is the same as the executable/module evidence map:
public evidence may justify a typed representation or experiment; it does not
automatically justify a PS5-specific behavior.

## Evidence classes

- **Official platform fact** — published by Sony for PS5 hardware.
- **Vendor ISA fact** — published by AMD for generic RDNA2 shader ISA.
- **Host API/compiler fact** — Khronos, LLVM, or Mesa documentation describing
  the host/toolchain side Astraea may target.
- **Cross-project observation** — independently visible architecture/format
  choices in public projects.
- **Hardware-backed community observation** — a public project explicitly
  reports using an observed representation on PS5 hardware.
- **Hypothesis / unknown** — insufficient evidence for Astraea to encode the
  behavior.

No community source listed here is an official PS5 graphics specification.

## Primary/public sources

### Sony PS5 hardware specifications

Source:

- https://blog.playstation.com/2020/03/18/unveiling-new-details-of-playstation-5-hardware-technical-specs/

Sony publicly specifies:

- an AMD Radeon RDNA 2-based graphics engine;
- hardware ray-tracing acceleration;
- variable GPU frequency up to 2.23 GHz;
- 10.3 TFLOPS;
- 16 GB GDDR6 system memory;
- 448 GB/s memory bandwidth.

These facts establish the hardware family and broad capability envelope. They
do **not** specify:

- a GFX IP identifier;
- shader initial-register ABI;
- command-processor packet dialect;
- register addresses/defaults;
- texture/buffer descriptor bit layouts;
- tiling/DCC rules;
- queue/fence semantics;
- Sony AGC container or API structures.

### AMD RDNA2 ISA reference

Source:

- AMD document 70648, "RDNA 2" Instruction Set Architecture: Reference Guide
- https://docs.amd.com/v/u/en-US/rdna2-shader-instruction-set-architecture

Release date: 2020-11-30.

This is the strongest public source for generic RDNA2 instruction semantics. It
covers the shader-machine model and instruction families including scalar and
vector ALU, scalar/vector/flat memory operations, LDS/GDS behavior, control
state, and exports.

It is a **generic RDNA2 ISA reference**, not a PS5 command processor or shader
ABI specification.

### Khronos SPIR-V

Source:

- SPIR-V 1.6 unified specification, Revision 8
- https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html

SPIR-V is a typed binary intermediate language for graphics shaders and compute
kernels. It models functions/control-flow graphs, structured control flow,
typed objects, storage classes, load/store operations, and SSA intermediate
results.

This makes SPIR-V a plausible **host-facing lowered representation** for
Astraea. It does not make SPIR-V a suitable representation for every guest
RDNA2 semantic detail.

### Khronos Vulkan

Source:

- Vulkan 1.4.363 specification, 2026-09-18
- https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html

Vulkan defines the host graphics/compute pipeline and its SPIR-V environment.

For Astraea this is a target contract, not evidence of guest PS5 behavior.
Vulkan handles, synchronization primitives, descriptor sets, render passes,
pipeline objects, and implementation capabilities must not leak into the
guest-facing semantic model.

### LLVM AMDGPU

Source:

- https://llvm.org/docs/AMDGPUUsage.html

LLVM publicly classifies the desktop RDNA2 family as GFX10.3 and lists targets
such as `gfx1030`, `gfx1031`, and related variants.

This is useful toolchain vocabulary. It does **not** prove that PS5 should be
modeled as exactly desktop `gfx1030` in every feature, register, packet, or
ABI detail.

### Mesa AMD/RADV/ACO/NIR

Sources:

- https://docs.mesa3d.org/drivers/radv.html
- https://docs.mesa3d.org/sourcetree.html

Mesa provides a useful public compiler/driver architecture precedent:

- AMD-specific hardware support is separated from API-facing Vulkan code;
- ACO provides an AMD shader compiler;
- NIR is a semantic compiler IR;
- SPIR-V tooling and Vulkan translation are separate layers.

Mesa is useful for architecture and compiler-engineering lessons, not as PS5
evidence.

## Community sources pinned for this review

### SharpProspero

Repository:

- https://github.com/SvenGDK/SharpProspero

Pinned commit:

`9220876e25bc28aca1f65ea644783a479949ad77`

Relevant files:

- `docs/graphics-gpu.md`
- `src/SharpProspero/Graphics/Agc/ShaderBinary.cs`
- `src/SharpProspero/Graphics/Agc/BuiltInShaders.cs`
- `src/SharpProspero/Graphics/Renderer3D.cs`

Permalinks:

- https://github.com/SvenGDK/SharpProspero/blob/9220876e25bc28aca1f65ea644783a479949ad77/docs/graphics-gpu.md
- https://github.com/SvenGDK/SharpProspero/blob/9220876e25bc28aca1f65ea644783a479949ad77/src/SharpProspero/Graphics/Agc/ShaderBinary.cs

SharpProspero currently describes an AGC-oriented layer in which resources and
pipeline state are represented by small register/descriptor blocks, commands
are recorded into command buffers, and buffers are submitted to the GPU. It
also exposes a shader-container abstraction separating a header from shader
microcode and reports direct PS5-oriented rendering usage.

**Evidence class:** hardware-oriented community observation.

This is stronger than generic desktop Radeon precedent, but it is still not an
official specification.

### Kyty

Repository:

- https://github.com/InoriRus/Kyty

Pinned commit:

`4733b7e1c91b10554a52007903d74dc76c39a230`

Relevant files include:

- `source/emulator/src/Graphics/Pm4.cpp`
- `source/emulator/include/Emulator/Graphics/Pm4.h`
- `source/emulator/src/Graphics/GraphicsRun.cpp`
- `source/emulator/src/Graphics/GraphicsRender.cpp`
- `source/emulator/src/Graphics/Shader.cpp`
- `source/emulator/src/Graphics/ShaderParse.cpp`
- `source/emulator/src/Graphics/ShaderSpirv.cpp`

Kyty independently demonstrates the general architecture of:

- packet/register-state parsing;
- draw/compute command handling;
- shader parsing;
- shader semantic translation;
- SPIR-V generation;
- Vulkan execution.

Because Kyty is older and has mixed PS4/PS5 ancestry, its constants and
behaviors are **corroborating observations**, not authoritative PS5 semantics.

### ps5link-sdk

Repository:

- https://github.com/Rufidj/ps5link-sdk

Pinned commit:

`ea771e535378740b6a058b8e5419eb8a0e0e0ec8`

The public project reports building AMD GPU assembly with public LLVM tooling,
targeting `gfx1030`, packaging the result into AGC-style shader containers,
and exercising custom graphics samples on PS5 hardware.

The shader-container/tooling lineage is derived from SharpProspero, so this is
**not independent evidence** for the container representation. It is useful as
an additional hardware-backed observation that GFX10.3-oriented public
toolchain output can participate in the tested PS5 path.

## Evidence map

### 1. PS5 is RDNA2-based, but "PS5 equals gfx1030" is too strong

**Evidence**

Sony officially identifies the GPU as RDNA2-based. AMD documents generic RDNA2
ISA behavior. LLVM groups desktop RDNA2 targets under GFX10.3, including
`gfx1030`. Public PS5 homebrew tooling reports successful use of
`gfx1030`-targeted shader assembly.

**Confidence**

- RDNA2 family: **high / official**.
- GFX10.3 as useful public compiler family: **high**.
- `gfx1030` as a practical public toolchain target for some PS5 shader
  experiments: **medium-high community evidence**.
- every PS5 GPU feature/register/packet behaving exactly like desktop
  `gfx1030`: **unsupported**.

**Astraea implication**

Astraea may use the public RDNA2/GFX10.3 ISA as a decoder starting point, but it
must keep a PS5 graphics profile distinct from desktop Radeon device profiles.

### 2. Instruction semantics and shader ABI are separate problems

**Evidence**

AMD's ISA document describes instruction semantics and architectural machine
state. It does not define the Sony application-facing stage ABI: which user
data, SGPRs/VGPRs, descriptors, stage metadata, or built-ins are initialized
for a PS5 shader invocation.

Community projects necessarily reconstruct such state, but there is no official
public PS5 shader-ABI source in this review.

**Confidence:** high.

**Astraea implication**

The first shader decoder must not bake stage-launch assumptions into opcode
decoding.

Keep separate:

```text
RDNA2 instruction decoder
PS5 shader metadata/container parser
PS5 stage ABI / launch-state builder
Shader IR
```

A decoded instruction can be correct while the shader still executes
incorrectly because launch state is wrong.

### 3. A packet/register-state graphics frontend is justified; exact packets are not

**Evidence**

SharpProspero exposes a command-buffer/register-block programming model.
Kyty independently contains PM4-like packet parsing, shader/context register
state, draw/dispatch submission, and PS5-specific branches.

This cross-project convergence supports a frontend that consumes guest command
streams and produces semantic state changes.

**Confidence:** medium-high architectural evidence.

**Unknown**

The review does not establish:

- which generic PM4 packet opcodes are valid on PS5;
- Sony-specific packet extensions;
- complete register address maps;
- reset/default values;
- state inheritance rules;
- malformed-packet behavior;
- command-buffer nesting/indirection semantics.

**Astraea implication**

Do not expose raw packet dwords directly to the Vulkan backend.

The frontend should retain both:

1. exact raw packet provenance;
2. a typed semantic operation when the packet is understood.

Unknown packets should produce typed unsupported/unknown evidence, never silent
best-effort interpretation.

### 4. Shader containers must remain separate from shader microcode

**Evidence**

SharpProspero explicitly models a compiled shader container as metadata/header
plus microcode. ps5link uses related tooling and reports hardware execution of
custom shaders.

**Confidence:** medium hardware-backed community evidence.

**Unknown**

- complete container schema;
- versioning rules;
- exact stage metadata;
- embedded resource/layout metadata;
- linkage across stages;
- firmware/toolchain variation.

**Astraea implication**

The future interface should look conceptually like:

```text
raw shader artifact
 -> container parser
 -> preserved metadata + microcode span
 -> RDNA2 decoder
```

Do not make the RDNA2 decoder understand Sony container bytes.

### 5. Resource descriptors are a separate evidence surface

**Evidence**

SharpProspero exposes typed buffer, texture/image, sampler, render-target, and
user-data concepts. Generic AMD ISA documentation explains instructions that
consume resources, but does not specify a PS5 application's descriptor ABI.

**Confidence:** medium.

**Unknown**

- exact descriptor word/bit layouts across resource classes;
- address-width and alignment rules;
- format encoding;
- swizzle semantics;
- bounds behavior;
- metadata/DCC fields;
- descriptor-table placement and pointer conventions.

**Astraea implication**

Use explicit descriptor types with raw-word preservation and validity state.
Do not translate unknown raw descriptor words directly into Vulkan descriptors.

### 6. Shader IR is necessary between RDNA2 and SPIR-V

**Evidence**

RDNA2 machine code exposes machine-specific state and control behavior that is
not naturally identical to SPIR-V's typed SSA model. Kyty independently uses a
parse/recompile path and generates SPIR-V. Mesa similarly uses semantic compiler
IRs rather than treating ISA bytes as an API-level shader.

**Confidence:** high architectural conclusion.

**Astraea implication**

The shader pipeline should be:

```text
RDNA2 bytes
 -> decoded instructions
 -> Shader IR
 -> SPIR-V
 -> Vulkan
```

Shader IR should be able to represent, at minimum:

- typed scalar/vector values;
- SGPR/VGPR-origin semantics without requiring physical host registers;
- EXEC/lane masking;
- control-flow edges;
- scalar/vector/flat memory operations;
- LDS/shared-memory operations;
- resource/image operations;
- stage inputs/outputs;
- exports;
- barriers and observable memory ordering;
- typed unsupported instructions.

Raw instruction bytes and original PC/offset remain provenance and trace
identity.

### 7. Graphics IR should be above packets and below Vulkan

**Evidence**

The guest side is command/state oriented, while Vulkan is host API/pipeline
oriented. A direct packet-to-Vulkan translation would couple unknown PS5 packet
details to host API implementation choices and make differential testing
difficult.

**Astraea implication**

Candidate Graphics IR operations include semantic actions such as:

- bind shader;
- bind resource/table;
- set render/depth target;
- set viewport/scissor;
- set raster/blend/depth state;
- draw;
- indexed draw;
- dispatch;
- resource transition/barrier;
- explicit synchronization point.

Graphics IR should **not** contain:

- Vulkan object handles;
- host memory pointers;
- raw PM4 dword arrays as its semantic identity.

Unknown raw packets/registers may be attached as diagnostic/provenance records,
not silently discarded.

### 8. Vulkan is a backend, not the emulator's graphics specification

**Evidence**

Khronos defines Vulkan and its SPIR-V environment. Mesa demonstrates the amount
of architecture needed between AMD hardware semantics and Vulkan execution.

**Astraea implication**

Keep host-backend choices behind an interface. Even if Vulkan is the only
backend initially, Graphics IR and Shader IR should describe guest-observable
semantics rather than Vulkan conveniences.

This also leaves room for:

- host feature fallbacks;
- offline IR tests;
- trace comparison without a GPU;
- future non-Vulkan analysis tools.

### 9. Surface layout, tiling, compression, and metadata remain high-risk unknowns

**Evidence**

Public AMD/Mesa infrastructure demonstrates that AMD surface layout is a
substantial problem in its own right. Community PS5 tooling exposes tile/layout
helpers, but this review does not establish complete PS5 rules.

**Unknown**

- PS5 tile/swizzle modes;
- mip/slice placement;
- depth/stencil layouts;
- multisample layout;
- DCC/color compression metadata;
- HTILE/depth metadata;
- import/export of display surfaces;
- CPU-visible versus GPU-native layout transitions.

**Astraea implication**

Do not bury surface addressing inside texture sampling code.

A future surface-layout component should be independently testable using
synthetic dimensions/formats and controlled readback experiments.

### 10. Synchronization/coherency needs explicit evidence

**Evidence**

Generic Vulkan and AMD hardware both expose nontrivial memory-ordering and
cache behavior. Community projects implement synchronization pragmatically, but
the exact PS5 guest contract is not publicly settled here.

**Unknown**

- cache flush/invalidate packet semantics;
- CPU/GPU visibility requirements;
- queue ordering;
- end-of-pipe/event behavior;
- fences/semaphores;
- command-buffer completion semantics;
- interaction with presentation.

**Astraea implication**

Graphics IR must reserve explicit synchronization operations rather than
assuming every draw/dispatch is globally ordered.

A host backend must not strengthen ordering invisibly in a way that masks guest
synchronization bugs during differential testing.

### 11. Presentation/VideoOut should not be conflated with GPU execution

**Evidence**

Public PS5-oriented tooling separates GPU command submission from display/
VideoOut operations.

**Confidence:** medium-high architectural evidence.

**Astraea implication**

Treat presentation as a system/HLE boundary that consumes a graphics resource,
not as a core graphics packet.

A future flow should resemble:

```text
guest GPU work
 -> completed renderable resource
 -> presentation/VideoOut service
 -> host swapchain/presentation
```

### 12. Ray tracing is officially present but its application contract is unknown

**Evidence**

Sony officially states that hardware ray-tracing acceleration is integrated in
the PS5 GPU.

**What that does not establish**

- public application API representation;
- acceleration-structure layout;
- shader-stage ABI;
- command packets;
- descriptor formats;
- exact ISA exposure;
- synchronization requirements.

**Astraea implication**

Do not implement PS5 ray tracing by projecting desktop Vulkan or Radeon APIs
onto the guest.

Ray tracing remains an explicit unsupported capability until stronger evidence
or controlled observations exist.

## Candidate architecture after #9

The evidence supports this separation:

```text
guest command-buffer bytes
        |
        v
PS5 graphics frontend
  - packet decode
  - register/state model
  - raw provenance preservation
        |
        v
Graphics IR
  - semantic state
  - draws/dispatches
  - resources
  - synchronization
        |
        +-----------------------+
        |                       |
        v                       v
shader container parser       trace adapter
        |
        v
RDNA2 instruction decoder
        |
        v
Shader IR
        |
        v
SPIR-V lowering
        |
        v
Vulkan backend
```

Trace hooks should exist at the guest-frontend and IR boundaries so a divergence
can be localized before host Vulkan behavior obscures it.

## First implementation boundaries justified by the evidence

### Graphics frontend slice

A safe first graphics code slice is **data-only**:

- raw command-buffer span;
- typed packet header;
- packet kind classification;
- preserved raw words;
- typed unknown/unsupported packet;
- semantic state object with no Vulkan dependency;
- deterministic parser failures;
- synthetic packet fixtures only.

Do not implement broad draw translation in this slice.

### Shader slice

A safe first shader code slice is:

- bounded RDNA2 instruction fetch;
- generic RDNA2 instruction classification from public AMD ISA;
- decoded operands/immediates;
- raw instruction preservation;
- typed unsupported opcode;
- synthetic public-ISA fixtures;
- no PS5 stage-launch assumptions;
- no Sony shader-container assumptions in the decoder.

### IR slice

Freeze the minimum Graphics IR / Shader IR contracts with tests before a Vulkan
backend exists.

This lets Astraea ask:

> Did guest interpretation diverge?

before asking:

> Did Vulkan render something different?

## PS5-specific unknowns that require stronger evidence

Do not hard-code answers to these from desktop Radeon precedent alone:

- exact command-packet dialect and PS5-only packet forms;
- graphics/register address map and reset defaults;
- state inheritance and command-buffer indirection;
- shader container versions and metadata;
- vertex/pixel/compute stage launch ABI;
- initial SGPR/VGPR/user-data state;
- wave-size selection and stage restrictions;
- descriptor bit layouts and table conventions;
- texture/sampler format and swizzle encoding;
- render/depth target descriptors;
- tiling/swizzle rules;
- DCC/HTILE or equivalent metadata behavior;
- barrier/cache/fence/event semantics;
- queue/ring submission semantics;
- compute dispatch details;
- NGG/geometry pipeline behavior;
- ray-tracing programming model;
- GPU virtual-address/coherency/fault behavior;
- presentation/flip ownership and synchronization;
- firmware/toolchain-version differences.

## Controlled experiments implied by the evidence

These should use Astraea-owned synthetic inputs and should record
AstraeaProbe + Trace v0 results so #7 can locate the first divergence.

### Command/state experiments

- submit the smallest accepted command buffer;
- one known state write followed by readback-observable work;
- unknown packet/opcode rejection behavior;
- state persistence across two submissions;
- command-buffer chaining/indirection if a lawful public path exposes it.

### Raster experiments

- clear a render target to a constant color and read back;
- minimal triangle with fixed coordinates;
- viewport/scissor boundary matrix;
- blend/depth enable matrix;
- render-target format matrix.

### Compute experiments

- dispatch a single workgroup writing a known pattern;
- vary workgroup dimensions and bounds;
- explicit producer/consumer sequence to expose synchronization.

### Shader ISA microprobes

Using only public AMD ISA/tooling and Astraea-owned shaders:

- scalar ALU;
- vector ALU;
- branches/control flow;
- EXEC/lane-mask behavior;
- scalar/vector/flat memory;
- LDS/shared memory;
- exports;
- selected image/buffer operations after descriptor evidence improves.

Each probe should isolate one observable semantic question.

### Resource experiments

- buffer descriptor length/bounds edge cases;
- texture dimensions and mip levels;
- sampler addressing/filtering matrix;
- format/swizzle matrix;
- descriptor-table placement changes.

### Surface-layout experiments

- small deterministic 2D surfaces;
- varying width/height/alignment;
- mip/slice progression;
- GPU-write / CPU-readback;
- CPU-write / GPU-readback.

Only observations/digests belong in the repository, not proprietary captures.

### Synchronization experiments

- write then read without explicit barrier;
- same sequence with candidate barrier;
- queue completion visibility;
- CPU/GPU visibility boundaries;
- repeated deterministic runs to distinguish stable behavior from timing noise.

## Falsification rules

A graphics assumption should be removed or narrowed when:

- an owned synthetic hardware observation contradicts it;
- two independent public sources disagree materially;
- behavior differs across tested firmware/toolchain versions;
- a supposedly stable field produces nondeterministic Trace v0 evidence without
  an explained cause;
- a Vulkan-specific workaround is being mistaken for guest semantics.

Unknown behavior must remain typed and observable rather than being converted
into a permissive fallback.

## What not to copy from another emulator

Astraea may study architecture and compare observations, but should not copy:

- packet-handler implementations;
- shader translators;
- register tables lacking independent provenance;
- guessed constants;
- game-specific hacks;
- compatibility patches whose behavioral basis is unclear.

If a value or behavior matters, Astraea should be able to point to public
documentation, independent convergence, or a controlled observation.

## Recommended next engineering sequence

1. Finish this evidence map (#9).
2. Define the first **graphics frontend evidence slice** as a new issue:
   typed packet/header/raw-preservation infrastructure with synthetic fixtures.
3. In parallel, continue the data-only SCE executable metadata parser implied
   by #8.
4. Add a minimal RDNA2 decoder issue using only AMD-published ISA fixtures.
5. Freeze minimal Graphics IR and Shader IR contracts.
6. Add trace adapters at frontend/IR boundaries.
7. Only after those layers are proven, begin a Vulkan backend.
8. Defer broad PS5 shader ABI, tiling, and ray-tracing behavior until evidence
   or controlled probes justify it.

## Current conclusion

There is enough public evidence to justify Astraea's **graphics architecture**:

- a PS5-specific command/state frontend;
- a separate generic RDNA2 instruction decoder;
- a semantic Graphics IR;
- a semantic Shader IR;
- SPIR-V lowering;
- Vulkan as a host backend;
- Trace v0 hooks before host lowering.

There is **not** enough public evidence to justify a broad PS5 GPU
implementation, complete packet/register tables, PS5 shader ABI, descriptor
layouts, tiling/compression rules, synchronization model, or ray-tracing
programming interface.

The next GPU work should therefore be narrow, typed, synthetic, and
evidence-producing rather than compatibility-driven.
