# ADR 0006: Dependency-driven vertical integration architecture

**Status:** Accepted  
**Date:** 2026-09-22

## Context

Astraea began with a deliberately simple milestone waterfall: establish the
loader and controlled x86-64 execution, expand HLE, then build graphics, then
integrate titles. That ordering was useful while interfaces were still
undefined.

The repository has now crossed that bootstrap boundary. Controlled native
execution, exact SCE import binding, Trace v0, a typed graphics frontend,
generic RDNA2 decode/Shader IR/CFG semantics, mixed wave execution, and the
first evidence-backed PS5 AGC shader-container ingestion path all exist.
Meanwhile, broad platform HLE remains intentionally incomplete.

Continuing to require "finish HLE before graphics" would now delay the
end-to-end feedback needed to discover real dependencies. The opposite
extreme—adding isolated opcodes, HLE calls, or backend features because they
are easy—would also waste effort.

Astraea therefore needs a durable rule for deciding what to build next while
keeping guest semantics independent from host implementation.

## Decision

Astraea will use **dependency-driven vertical integration**.

The roadmap is a set of stable architectural boundaries and end-to-end gates,
not a promise that entire subsystems are completed in a fixed sequence. After
each merge, the next implementation issue is selected by asking:

> What is the smallest real missing dependency preventing the next vertical
> integration gate?

### System boundaries

#### CPU / platform path

```text
SCE ELF/module
    -> validated GuestImage
    -> native x86-64 guest execution
    -> exact import/HLE dispatch
    -> platform services
```

#### Shader path

```text
PS5 AGC shader container
    -> typed AGC shader object + provenance
    -> bounded RDNA2 program
    -> generic RDNA2 decoder
    -> Shader IR + CFG
    -> host-shader lowering
    -> validated SPIR-V
```

#### GPU command/state path

```text
guest AGC calls / submitted DCB bytes
    -> PS5 AGC command + register/state frontend
    -> Astraea Graphics IR / guest GPU state
    -> guest resource model
    -> Vulkan backend
```

#### Output path

```text
Vulkan render/compute result
    -> guest-visible synchronization
    -> VideoOut/presentation model
```

#### Verification path

```text
public evidence / controlled probe
    -> raw capture
    -> normalized Trace v0 events
    -> first-divergence diff
    -> regression corpus
```

### Architectural invariants

1. **Guest-domain semantics stay above host APIs.** SCE/AGC objects, commands,
   registers, resources, synchronization, and shader semantics are guest
   concepts. Vulkan is a backend. An `sceAgc*` call must not become a hidden
   direct-to-Vulkan contract.

2. **PS5-specific state stays separate from generic RDNA2 semantics.** AGC
   containers, command streams, launch metadata, and register conventions are
   PS5-specific. AMD-documented RDNA2 instructions are guest ISA semantics.
   "Generic RDNA2" means shared AMD-defined behavior, not placeholder behavior.

3. **Shader IR is the semantic boundary; the wave interpreter is an oracle.**
   The existing interpreter remains useful for exact tests, differential
   checks, and difficult instruction semantics. It is not intended to execute
   every production shader instruction lane-by-lane during normal rendering.
   The production path lowers supported Shader IR to host shader code, with
   SPIR-V as the first backend IR.

4. **Guest resource identity exists above Vulkan.** Buffers, images, samplers,
   descriptors, aliases, layouts, lifetimes, and guest-visible synchronization
   are modeled as guest GPU state. The Vulkan backend materializes and
   synchronizes host resources from that model.

5. **Command submission is an explicit boundary.** Public AGC builder calls are
   useful evidence and instrumentation points, but Astraea must retain the
   ability to decode submitted guest-visible command-buffer bytes. It must not
   depend permanently on intercepting every high-level builder call.

6. **Provenance survives irreversible boundaries.** Raw container/command
   bytes, decoded PS5 state, RDNA2 instructions, Shader IR, emitted SPIR-V, and
   backend state retain enough identity to explain a divergence.

7. **Coverage is workload-driven.** New RDNA2 instructions, floating-point
   modes, HLE services, descriptors, and synchronization features are added
   because a concrete vertical workload requires them—not to maximize a
   subsystem coverage counter.

8. **Unknown behavior remains explicit.** An undocumented Sony-specific field
   or behavior becomes a named evidence blocker. Astraea does not invent a
   plausible constant merely to keep execution moving.

### Vertical integration gates

The gates are ordered by dependency, but workstreams may advance in parallel
when their interfaces are stable.

- **V0 — PS5 shader ingestion.** AGC container -> bounded RDNA2 -> Shader IR.
  Completed by #134/#135.
- **V1 — Guest-created shader object.** An Astraea-owned SCE probe reaches the
  shader-creation HLE boundary and produces a guest-domain Astraea AGC shader
  object containing validated stage/program/provenance state, with no Vulkan
  dependency.
- **V2 — Validated host shader module.** A concrete supported Shader IR subset
  lowers deterministically to Vulkan-environment SPIR-V and passes SPIR-V
  validation.
- **V3 — Headless GPU execution.** A controlled guest-domain graphics or
  compute workload flows through command/state, resources, emitted SPIR-V, and
  Vulkan to a deterministic host-visible result.
- **V4 — Controlled PS5 differential.** The same owned workload can be observed
  on authorized PS5 hardware, when available, and compared at stable
  trace/state/image boundaries. Hardware is a validation accelerator rather
  than a prerequisite for V0-V3.
- **V5 — Presentation.** Guest draw/flip state reaches the VideoOut model and a
  host presentation path without title-specific hacks.

### Parallel workstreams

Parallel work is allowed across stable interfaces:

- loader/execution/platform HLE needed by the next owned probe;
- AGC shader/container/object research;
- shader compiler/SPIR-V backend;
- guest GPU command/state/resources;
- Trace/Lab/differential infrastructure.

Do not parallelize competing guesses about the same undocumented boundary.

## Consequences

### Positive

- Work is pulled by end-to-end dependencies instead of arbitrary subsystem
  completeness.
- Generic AMD semantics remain useful because they are connected to a real PS5
  frontend rather than developed in isolation.
- The architecture can absorb new evidence without rewriting the entire plan.
- PS5-specific uncertainty is localized instead of leaking into Vulkan or
  generic RDNA2 code.
- Hardware access improves validation when available but does not halt
  architecture work that is already publicly evidenced.
- Visible progress has stronger meaning: each vertical gate proves a longer
  real path through the emulator.

### Negative / constraints

- Milestone numbers no longer imply that one subsystem is globally "finished."
- Some capabilities will remain intentionally narrow until a workload demands
  them.
- A vertical gate may expose an evidence blocker that forces research before
  more code can be justified.
- Maintaining provenance and separate guest/backend models adds interfaces that
  a compatibility-first emulator might initially skip.

## Alternatives considered

### Finish all platform HLE before graphics

Rejected as a strict sequencing rule. The graphics interfaces are now mature
enough to progress independently, and broad HLE work should be pulled by
owned probes and integration needs.

### Maximize RDNA2 opcode coverage first

Rejected. Instruction semantics are real work, but coverage without a shader
ingestion/compiler/resource path can become disconnected from the shortest
route to rendering.

### Translate AGC/HLE calls directly to Vulkan

Rejected as the architectural contract. Public AGC examples show command
builders writing guest-visible command buffers that are later submitted.
Direct interception may be useful for diagnostics or temporary controlled
experiments, but it cannot replace a guest command/state model.

### Compatibility-first title patching

Rejected as the primary strategy by ADR 0001. Booting a title is an integration
signal, not proof of semantic correctness.

## Evidence / references

- Sony PS5 technical specifications:
  https://blog.playstation.com/2020/03/18/unveiling-new-details-of-playstation-5-hardware-technical-specs/
- LLVM AMDGPU backend / ELF model:
  https://llvm.org/docs/AMDGPUUsage.html
- Khronos SPIR-V specification:
  https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
- Vulkan shader modules / Vulkan environment for SPIR-V:
  https://github.khronos.org/Vulkan-Site/spec/latest/chapters/shaders.html
- Public native PS5 AGC homebrew and hardware-used shader path:
  https://github.com/Rufidj/ps5link-sdk
- Public AGC model / tooling used as behavioral evidence:
  https://github.com/SvenGDK/SharpProspero
- Astraea AGC evidence note:
  `docs/research/ps5_agc_shader_container.md`
- Architecture refresh issue #136.
- ADR 0001, ADR 0004, and ADR 0005.

Third-party implementations are evidence/context only. Astraea does not copy
their emulator implementation.

## Revisit triggers

Revisit this decision if:

- controlled PS5 observations contradict a boundary assumed here;
- a Vulkan-independent guest resource/state model proves unable to represent
  required guest behavior;
- SPIR-V becomes unsuitable for the first host shader backend;
- the wave interpreter is shown to be required in a materially different role;
- vertical-gate planning repeatedly creates measurable critical-path overhead
  without improving correctness or integration speed.
