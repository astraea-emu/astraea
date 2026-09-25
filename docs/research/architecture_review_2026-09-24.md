# Architecture review — 2026-09-24

## Purpose

Stress-test Astraea's post-C0 architecture and roadmap against the current
repository, public PS5-oriented projects, a mature modern console emulator, and
Mesa's AMD compiler architecture.

This document is **architecture comparison**, not a source of PS5 behavioral
truth. Another emulator's implementation is never promoted into Astraea as
Sony-specific evidence without independent public or controlled support.

## Astraea snapshot reviewed

Repository: `astraea-emu/astraea`  
Frontier reviewed after production Linux retail diagnostic merge.  
Relevant durable decisions: ADR 0006-0011.

At this point Astraea has:

- strict PS5/SCE image parsing and typed preflight;
- exact import/HLE foundations;
- native owned x86-64 probes;
- a separate supervised retail worker;
- Linux pre-kernel guest-syscall containment;
- sealed retail artifact authority and finite resource policy;
- deterministic typed diagnostic/fault boundaries;
- V0-V3 graphics integration through submitted PM4/state/resources and real
  Vulkan readback;
- separate semantic Shader IR and compiler/value IR;
- raw provenance through the graphics path;
- a production Linux `astraea diagnose <artifact>` interface that stops an
  otherwise-ready image at `unsupported_initial_process_abi`.

## Comparison set

### shadPS4

Repository: https://github.com/shadps4-emu/shadPS4  
Pinned commit: `612713d5ff6173f7028abcc4009bd5a76d0fd80a`

Why it is useful:

- mature direct-title PS4 HLE emulator with native x86-64 execution;
- real-game compatibility pressure;
- broad runtime linker/TLS/platform-service implementation;
- modern AMD shader recompiler with explicit frontend, IR, passes, and SPIR-V
  backend;
- substantial GPU resource/cache/scheduling infrastructure.

At the pinned revision the source tree includes dedicated linker/TLS code and a
shader compiler with CFG/structured-control-flow components, an explicit IR,
dominance/phi/resource/lowering passes, and SPIR-V emission. The project README
also explicitly credits yuzu's Hades compiler as a blueprint for its shader
compiler.

**Lesson for Astraea:** the expected *classes* of later dependencies are real:
module linking, TLS, platform services, resource tracking/caching,
synchronization, and a richer optimizing shader compiler eventually become
load-bearing under game workloads.

**Do not copy:** PS4 ABI values, libraries, kernel behavior, PM4 semantics,
shader assumptions, or title workarounds as PS5 truth.

### RPCSX

Repository: https://github.com/RPCSX/rpcsx  
Pinned commit: `e8ae1481ab7ba04d5c6bef89dd852aabba2c88ff`

Kernel companion: https://github.com/RPCSX/orbis-kernel  
Pinned commit: `6b6bbad67d0d6ef5f7d91a7a3c87a9b8a2af8a2b`

Why it is useful:

- demonstrates a different architectural choice: larger firmware/kernel/VSH
  oriented system emulation rather than only direct-title HLE;
- illustrates that VSH/firmware boot is itself a major engineering track.

**Lesson for Astraea:** do not make firmware/VSH boot the default critical path.
Direct-title diagnostics expose missing title dependencies with much less
unrelated system surface. A firmware track can be added later if a concrete
goal requires it.

### KytyPS5

Repository: https://github.com/KytyPS5/KytyPS5  
Pinned commit: `5a705dd15f9312f7db29baa91dfd93a2c895cb53`

This active PS5-focused Kyty lineage is a more relevant current architecture
comparator than the original 2022 tree. At the pinned revision its source tree
contains explicit:

- Prospero guest-GPU command processing and PM4 dispatch;
- separate guest-GPU and host-GPU layers;
- host memory/page tracking;
- buffer, texture, sampler, and pipeline caches;
- renderer command scheduling and synchronization;
- render-target/tiling infrastructure;
- shader decode/CFG/IR/translation and SPIR-V emission.

**Lesson for Astraea:** these are credible eventual dependency classes once
real titles create repeated resource lifetimes and synchronization pressure.
They reinforce Astraea's existing guest/host GPU separation and the plan to add
page tracking/caches/scheduling only when C4 workloads require them.

Compatibility claims from another emulator are not evidence for Astraea's PS5
semantics and are not used to promote register/API behavior.

### SharpEmu

Repository: https://github.com/sharpemu/sharpemu  
Pinned commit: `d4ff32a1d27d33afe7b7fb640b9d7567f16fea89`

SharpEmu is an active PS5-only experimental project. At the pinned revision its
source tree includes:

- native direct-execution profiles and guest-thread flow;
- PS5/SCE loading and imported-symbol relocation;
- guest memory/page protection and page tracking;
- guest TLS templates and module management;
- kernel/HLE synchronization, memory, file, socket, semaphore, and pthread
  surfaces;
- guest GPU buffer/image caches;
- shader and pipeline caches;
- GPU scheduling/timeline abstractions;
- Vulkan host GPU and VideoOut/presentation infrastructure.

**Lesson for Astraea:** independent PS5 work converges on the same broad
post-entry subsystems predicted by C2-C5. This strengthens the roadmap but does
not justify copying C# implementation details, PS5 constants, syscall behavior,
or compatibility assumptions.

### Kyty

Repository: https://github.com/InoriRus/Kyty  
Pinned commit: `4733b7e1c91b10554a52007903d74dc76c39a230`

The official README describes the project as early-stage, able to run some
simple PS4 games and PS5 homebrew, with major services still missing. Its
source includes a runtime linker, PM4 handling, shader parsing, SPIR-V
generation, and Vulkan-oriented rendering.

**Lesson for Astraea:** Kyty independently corroborates the broad shape
"runtime linker + command/register frontend + shader translation + Vulkan",
but its age, partial service coverage, and PS4/PS5 mixture make it a poor
template for modern Astraea internals.

### SharpProspero

Repository: https://github.com/SvenGDK/SharpProspero  
Pinned commit: `60977b29103452b9681ae10c7a9011d44965bf16`

SharpProspero is primarily useful as PS5-oriented hardware/toolchain evidence,
not as an emulator blueprint. Its source exposes AGC shader/container,
renderer, runtime/linker, CRT, and scheduler concepts.

**Lesson for Astraea:** keep Sony-specific AGC/runtime evidence separate from
generic AMD/RDNA2 evidence. Hardware-oriented community observations can guide
controlled probes, but do not establish undocumented behavior by themselves.

### ps5link-sdk

Repository: https://github.com/Rufidj/ps5link-sdk  
Pinned commit: `ea771e535378740b6a058b8e5419eb8a0e0e0ec8`

This project explicitly targets native PS5 **titles**, not only payloads. It
builds SCE ELF/dynamic metadata, imports functions by NID/module identity, and
contains a real-title startup object. Its README also explicitly reports that
thread-local storage is not handled by its current toolchain.

**Lesson for Astraea:** #300 is correctly placed. Initial process state,
runtime/module initialization, and TLS are genuine load-bearing surfaces before
an arbitrary retail image should enter native execution.

The CRT lineage is derived from SharpProspero, so ps5link + SharpProspero are
not independent corroboration of the complete startup ABI.

### Mesa RADV / NIR / ACO

Documentation:

- https://docs.mesa3d.org/drivers/radv.html
- https://docs.mesa3d.org/nir/index.html
- https://docs.mesa3d.org/sourcetree.html

RADV's documented compiler flow is broadly:

```text
SPIR-V
 -> NIR
 -> optimization/lowering/linking
 -> lower-level NIR
 -> ACO
 -> AMD ISA
```

NIR is an optimizing semantic compiler IR with reusable lowering/optimization
passes.

**Lesson for Astraea:** a nontrivial GPU compiler needs a real intermediate
compiler representation and passes. Astraea's separation between semantic
Shader IR and workload-driven compiler/value IR is sound. Do not bypass it by
translating guest ISA directly into ad-hoc SPIR-V patterns as compatibility
pressure grows.

## Findings

### 1. No foundational rewrite is justified

The current architecture has the right durable separations:

- structural loader vs runtime/module behavior;
- guest addresses/identity vs host pointers/resources;
- controller vs untrusted retail worker;
- guest syscall semantics vs host syscall mechanism;
- PS5 GPU frontend vs Vulkan;
- raw packet/state provenance vs typed Graphics IR;
- generic RDNA2 semantics vs PS5 AGC/stage ABI;
- semantic Shader IR vs compiler/value IR;
- guest images/layouts vs Vulkan resources.

Changing those boundaries now would create churn without removing a known
dependency.

### 2. The previous roadmap model was the real weakness

V0-V5 alone became misleading after CPU/retail work advanced independently.

Fix: ADR 0011 adds an orthogonal C0-C6 compatibility ladder.

This explicitly locates:

- first legal retail diagnostic — C0, complete;
- first native retail instruction — C1;
- module/TLS/HLE bootstrap — C2;
- deterministic boot — C3;
- first real-title headless GPU/frame — C4;
- visible presentation/menu — C5;
- in-game/playable/accuracy — C6.

### 3. #300 should be layered, not treated as one magic ABI

Split the evidence problem conceptually into:

- C1A entry registers + parameter block + teardown;
- C1B process/procparam metadata;
- C1C primary-thread TLS/TCB and FS/GS;
- C1D module/runtime bootstrap ordering.

Only promote fields supported by evidence and only implement the subset the
selected workload requires.

### 4. Runtime linker/module graph is likely the next major reusable abstraction

After C1, avoid stuffing runtime behavior into `GuestImage`.

A future module layer should own:

- loaded module identity and lifecycle;
- module/library/NID imports/exports;
- dependency graph;
- cross-module relocations;
- HLE-vs-LLE resolution policy;
- per-module initialization state.

This is strongly suggested by real-title toolchains and mature HLE emulators,
but implementation should wait until the first C1 workload reaches the need.

### 5. A real guest process/thread/object model will eventually be necessary

Likely later dependencies:

- guest process identity;
- guest thread contexts and TLS;
- handle/object table;
- waits/events/semaphores/mutexes;
- virtual-memory lifecycle;
- clocks/timers.

Do not map guest identity directly onto unmanaged host process/thread objects.

### 6. GPU cache/scheduler breadth should be delayed until C4 pressure

Mature emulators contain buffer/image caches, page tracking, pipeline caches,
resource discovery, and complex synchronization because real games require
them.

Astraea should expect these components, but not build them merely because
another project has them.

Trigger them when a real title first demonstrates:

- repeated/aliased guest resource use;
- guest CPU writes invalidating GPU resources;
- pipeline recreation pressure;
- queue/fence/barrier semantics;
- descriptor-table reuse;
- shader cache requirements.

### 7. Shader compiler maturation should be workload-driven

Keep semantic Shader IR stable as the guest-semantic/provenance layer.

When a real title requires it, expand compiler/value IR with:

- SSA/value flow;
- dominance and phis;
- structured control flow;
- stage I/O;
- resource discovery/lowering;
- compiler optimizations;
- stable cache keys.

Do not pre-port another project's compiler pass suite.

### 8. Full firmware/VSH boot should remain optional

RPCSX demonstrates that firmware/VSH is a valid but large direction. It should
not displace the current direct-title critical path.

Astraea should add such a track only if:

- a concrete title/runtime dependency cannot be modeled cleanly by direct
  title/HLE work; or
- firmware behavior itself becomes a research/product goal.

### 9. Compatibility reporting should begin with the first real title

Introduce normalized categories and environment metadata before compatibility
claims spread across issues/chats.

Recommended categories:

```text
diagnostic
native-entry
boot
first-frame/headless-GPU
menu
in-game
playable
accurate
```

Store hashes/metadata and normalized traces, not proprietary game bytes.

### 10. Security hardening remains layered defense, not emulator semantics

The Linux C0 boundary is sufficient for the current diagnostic threat model.
pidfd and Landlock remain valuable defense-in-depth tasks.

Do not let optional host-hardening work obscure the next guest-semantic blocker
(#300), and do not describe C0 as a complete malicious-code sandbox.

## What Astraea should not add now

- a speculative full PS5 syscall table;
- broad HLE service stubs returning success;
- a complete PM4/register table copied from desktop AMD or another emulator;
- a full firmware/VSH boot path as prerequisite;
- generic title hacks;
- speculative resource/pipeline caches before a workload needs them;
- a rewrite of semantic Shader IR;
- a broad Windows retail path without an equivalent pre-kernel syscall
  containment design;
- guessed LinkShaders tail values.

## Recommended next sequence

```text
#300 C1 initial-process evidence
  -> implement smallest corroborated entry profile
  -> first native retail instruction under existing Linux supervisor/seccomp
  -> observe first runtime dependency
  -> C2 module/TLS/HLE slice
  -> repeat first-missing-dependency loop
  -> C3 deterministic boot
  -> C4 first real-title headless GPU/frame
  -> V5/C5 presentation when actually required
```

In parallel, only when independent:

- #191/#207 LinkShaders evidence;
- #297 pidfd hardening;
- #298 Landlock evaluation;
- #168 repository governance.

## Conclusion

Astraea is on a sound architectural path.

The highest-value improvement is not a rewrite. It is keeping the next phase
strictly dependency-driven while making the compatibility milestones explicit.
The production diagnostic now gives the project a safe way to let a legally
obtained title identify the next real blocker; the repository should use that
signal rather than guessing a large emulator feature list.
