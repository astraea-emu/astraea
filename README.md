# Astraea

A verification-first PlayStation 5 compatibility research and emulation
project.

> **Status:** V0-V3 are complete for their bounded owned workloads. Astraea can
> now accept a user-selected executable through the production Linux x86-64
> `astraea diagnose <artifact>` path, move only sealed artifact bytes into a
> supervised worker, apply finite resource limits, and return a deterministic
> typed pre-entry diagnostic. A structurally ready PS5/SCE image deliberately
> stops at `unsupported_initial_process_abi`: retail native entry is **not**
> enabled until the PS5 process-entry contract is established. Astraea does
> not claim that retail titles boot, render, reach a menu, or are playable.

## Principles

- Clean-room implementation: no proprietary Sony source code, firmware, keys,
  SDK files, proprietary system modules, or copyrighted retail assets in this
  repository.
- Evidence before emulation: document public or controlled behavior before
  encoding PS5-specific assumptions.
- Guest semantics before host mapping: SCE/AGC/RDNA2 behavior remains separate
  from Vulkan and other host APIs.
- Native x86-64 execution where host architecture permits it; portable
  subsystems remain host-independent.
- Verification-first development: structured traces, differential tests,
  regression localization, fuzzing, sanitizers, and reproducible experiments.
- Dependency-driven vertical integration: build the smallest real dependency
  that advances the next end-to-end gate instead of maximizing isolated API or
  opcode coverage.
- Unknown behavior stays typed and explicit instead of becoming a plausible
  fallback.
- GitHub is the durable source of truth for architecture, status, decisions,
  evidence, and handoffs.

## Architecture

Astraea keeps guest-domain behavior separate from host implementation.

1. **Guest image / process path** — SCE ELF/module parsing, mappings,
   relocations, import identity, process-entry/TLS state, native x86-64
   execution, and HLE platform services.
2. **Supervised retail runtime** — controller/worker process boundary, sealed
   artifact authority, typed protocol, guest syscall interception, faults,
   time/resource limits, and deterministic diagnostic stops.
3. **PS5 GPU frontend** — AGC shader containers/objects, command buffers,
   register/state, guest resources, synchronization, and presentation state.
4. **Shader semantics/compiler** — AMD-documented RDNA2 decoding, semantic
   Shader IR and CFG, semantic-oracle execution for verified subsets, a
   separate workload-driven compiler/value IR, then SPIR-V lowering.
5. **Host GPU backend** — Vulkan resource/pipeline/synchronization
   materialization from the guest GPU model. Vulkan is not the guest API.
6. **Astraea Lab / Probe** — trace capture, normalization, first-divergence
   analysis, controlled owned programs, and lawful reference-hardware
   experiments when evidence requires them.

"Generic RDNA2" means AMD-defined ISA behavior shared by the hardware family.
It is real emulator behavior, not placeholder data. PS5 AGC metadata and stage
ABI remain separate Sony-specific evidence surfaces.

## Development hosts

Portable repository work targets macOS, Linux, and Windows.

Native PS5 x86-64 guest execution targets x86-64 hosts. Linux x86-64 is the
first platform admitted for arbitrary retail **diagnostics** because Astraea
has a verified pre-kernel guest-syscall containment boundary there. Windows
retains the owned synthetic native-execution/supervision proofs, but arbitrary
retail native entry is not admitted there yet.

Vulkan is the first host graphics backend and SPIR-V is the first host shader
module format. Semantic Shader IR remains backend-independent; compiler/value
IR is introduced only when a real workload requires structured dataflow,
stage I/O, resources, or other compiler concerns. See ADR 0007.

## Graphics integration gates

The bounded graphics path is now:

```text
V0  AGC container -> RDNA2 -> semantic Shader IR             COMPLETE
 |
 v
V1  owned guest -> persistent AGC shader identity             COMPLETE
 |
 v
V2  supported semantic Shader IR -> validated SPIR-V          COMPLETE
 |
 v
V3  submitted PM4/state/resources -> Vulkan -> exact result   COMPLETE
 |
 +--> V4 controlled PS5 differential when evidence requires it
 |
 `--> V5 guest flip/VideoOut -> host presentation             FUTURE
```

V3 completion is deliberately bounded: it proves an owned submitted raster
workload through decoded draw-time state, stage-qualified shader identity,
typed guest image backing, generated SPIR-V, real Vulkan execution, and exact
readback. It does **not** imply broad PS5 graphics compatibility.

The remaining `sceAgcLinkShaders` tail in #191 is a separate evidence track;
it is not guessed merely to make a title progress.

## Retail compatibility gates

Graphics gates and title-compatibility gates are orthogonal.

```text
C0  supervised production retail diagnostic                  COMPLETE (Linux x86-64)
C1  evidenced PS5 process-entry ABI -> first retail instruction
C2  runtime/bootstrap closure: modules, relocations, TLS, first HLE/syscall
C3  deterministic title boot / sustained initialization
C4  first real-title headless GPU submission / frame evidence
C5  VideoOut/presentation -> visible boot or menu
C6  in-game / playable / accuracy progression + compatibility reporting
```

The current critical path is **C1**, tracked by #300. Astraea already accepts a
legally obtained user artifact as a bounded diagnostic input, but a
structurally ready image intentionally stops before its first retail
instruction until the initial PS5 process state is supported by sufficient
evidence.

A compatibility category is an integration observation, not a correctness
oracle. Title-specific hacks do not replace missing guest semantics.

## Current strategy

- Prefer direct-title diagnostics over making full firmware/VSH boot a
  prerequisite.
- Pull module/HLE/thread/TLS/GPU breadth from the first missing dependency
  exposed by a real workload.
- Preserve raw provenance so later evidence can correct interpretations without
  recapturing inputs.
- Keep targeted reference-hardware probes available when they are the shortest
  way to resolve one PS5-specific blocker.
- Add resource caches, scheduler breadth, compiler passes, and presentation
  only when real title paths demand them rather than speculatively cloning
  another emulator's feature list.

See:

- `docs/PROJECT_PLAN.md`
- `docs/STATUS.md`
- `docs/adr/0010-supervised-retail-execution.md`
- `docs/adr/0011-orthogonal-graphics-and-compatibility-gates.md`
- `docs/research/architecture_review_2026-09-24.md`
- `docs/research/ps5_initial_process_abi.md`
- `docs/CHAT_HANDOFF.md`

## Compatibility boundary

The production Linux x86-64 diagnostic path is active. It is intentionally
narrow:

- controller reads the user-selected host path;
- worker receives sealed bytes, not the original path;
- finite worker resource policy is installed before guest RUN;
- guest-originated Linux syscalls are trapped before host-kernel execution;
- filesystem/network behavior is not ambiently inherited;
- typed loader/pre-entry/fault/syscall boundaries stop deterministically;
- a currently otherwise-ready PS5/SCE image stops at
  `unsupported_initial_process_abi`.

This is permission to **diagnose** legally obtained retail executables safely
under Astraea's documented threat model. It is not a claim that Astraea can
boot or play commercial PS5 games.

## License

Astraea is licensed under the GNU General Public License v3.0 or later. See
`LICENSE`.
