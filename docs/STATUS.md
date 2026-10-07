# Project Status

**Repository:** `astraea-emu/astraea`  
**Merged frontier:** production Linux x86-64 retail diagnostic path is active  
**Current critical path:** #300 — establish the PS5 initial-process ABI before retail native entry  
**Graphics:** V0-V3 complete for their bounded owned workloads  
**Compatibility:** C0 complete on Linux x86-64; C1 active  
**CI merge gate:** Linux x64, Windows x64, macOS ARM64, Linux ASan+UBSan, Linux Clang fuzz smoke

## What is complete

### Foundation and execution

- Strict PS5/SCE `GuestImage` parsing, mappings, dynamic metadata,
  relocations/import identity, initial-stack/TLS metadata, fuzzing, and typed
  failures.
- Controlled native x86-64 execution for Astraea-owned probes on Linux and
  Windows.
- Exact HLE/import gate infrastructure and owned SCE-profile execution proofs.
- Trace v0, first-divergence diffing, AstraeaProbe v0, provenance, ADRs, and the
  five-job CI discipline.

### Graphics V0-V3

- V0: AGC container -> generic RDNA2 decode -> semantic Shader IR.
- V1: owned guest -> real `sceAgcCreateShader` HLE -> persistent stage-aware
  created-shader identity.
- V2: supported semantic Shader IR -> deterministic Vulkan-valid SPIR-V.
- V3: submitted PM4/DCB bytes -> draw-time register state -> stage-qualified
  shader lookup -> compiler/value IR -> generated SPIR-V -> typed guest render
  target -> real Vulkan raster -> exact deterministic readback.
- Separate guest GPU allocations, image views, physical surface layout, and
  Vulkan materialization under ADR 0009.
- Real Vulkan buffer WRITE_DATA and offscreen/raster proofs execute in CI.

V3 completion is bounded to the proved workloads. It does not imply complete
PM4, descriptor, shader-ISA, synchronization, tiling/compression, or AGC
coverage.

### C0 supervised retail diagnostics

Linux x86-64 now has a production `astraea diagnose <artifact>` path with:

- separate controller and untrusted worker process;
- bounded versioned binary protocol;
- exact handle/fd inheritance policy and deterministic teardown;
- pidfd-backed stable worker identity/signalling when supported, with reviewed fallback;
- finite wall-clock and kernel resource ceilings;
- sealed artifact handoff: worker never receives the original host pathname;
- typed faults and terminal diagnostic events;
- pre-kernel seccomp containment for guest-originated syscall entry;
- typed syscall request/result mediation for the verified synthetic profile;
- deterministic retail preflight for loader, entry, dynamic dependencies,
  relocations, and TLS;
- automation-friendly diagnostic output;
- explicit unsupported-platform behavior outside Linux x86-64.

A structurally ready PS5/SCE image still stops at
`unsupported_initial_process_abi`. No retail instruction is executed merely
to make progress.

## What can be tested now

On Linux x86-64, a user may run the production diagnostic against a legally
obtained executable artifact:

```text
astraea diagnose <artifact>
```

This is now an intentional project milestone, not an unsafe prototype launch.
The expected result is a deterministic typed **first boundary**. For an image
that passes the current structural/dynamic/relocation/TLS preflight, Astraea
currently reports `unsupported_initial_process_abi` and does not execute the
retail entry point.

That result is useful: it confirms the production artifact/supervisor path and
identifies C1 as the next dependency. It is not a boot/playability result.

## Current critical path: C1

Issue #300 asks for the smallest evidenced PS5 initial-process contract needed
before retail native entry.

Promote it in layers rather than as one guessed ABI:

1. **C1A — entry register/parameter-block contract**
   - corroborate loader-provided RDI parameter block;
   - corroborate RSI teardown role;
   - bound argc/argv and stack observations.
2. **C1B — process metadata**
   - establish relationship to process/procparam metadata and ownership.
3. **C1C — primary-thread TLS/TCB**
   - establish required TLS allocation and initial FS/GS state.
4. **C1D — bootstrap ordering**
   - establish which module/import/runtime initialization must precede entry.

Only the subset required by the selected diagnostic workload should be
implemented. Unknown fields remain unsupported.

## 2026-10-07 re-entry review

A fresh comparison against current KytyPS5, Prosperity, SharpEmu, prosper,
ps5rs, PortPS5, ps5link/SharpProspero and current native-title tooling did not
invalidate Astraea's architecture. It did produce independent hardware-exercised
corroboration for the direct-title process-block prefix, but still not enough
evidence to enable retail entry.

The important refinement is that C1 must distinguish **bootstrap entry** from
the later **title entry**. Current public implementations do not agree on
where execution begins: one lineage synthesizes the title RDI/RSI envelope,
while another current PS5 implementation enters libkernel first with an
initial stack and FS/TCB state. These are comparative implementations, not
permission to guess either contract.

See `docs/research/scene_review_2026-10-07.md`. The critical path remains #300.
Two independent hardware-exercised native-title lineages now support a partial
C1A prefix: RDI/process-parameter block, argc-like field at +0, argv-like
vector at +8, and the original process pointer passed to runtime environment
initialization. This prefix may be encoded as typed Astraea state.

Still unresolved and entry-blocking: mandatory RSI teardown semantics, exact
initial RSP/stack state, primary-thread TLS/TCB/FS-GS, and bootstrap ordering.

## After C1

The first real title, not a speculative feature checklist, determines the next
dependency:

- module graph / runtime linker and HLE-vs-LLE resolution policy;
- relocation/import completion;
- guest process/thread/handle model;
- TLS and synchronization;
- filesystem/time/input/audio/video services;
- broader RDNA2/compiler/resource support;
- page tracking, resource/pipeline caches, scheduling and synchronization;
- VideoOut/presentation.

The durable compatibility ladder is:

```text
C0 diagnostic -> C1 first retail instruction -> C2 bootstrap/HLE closure
 -> C3 boot -> C4 first headless title GPU/frame evidence
 -> C5 visible presentation/menu -> C6 in-game/playable/accuracy
```

## Independent evidence tracks

### LinkShaders (#191 / #207)

Guest-visible `sceAgcLinkShaders` success remains intentionally capped at the
measured output. CX[32] and UC[0..2] require controlled/reference-hardware
evidence. Candidate public register identities do not establish returned
values.

This work should advance when it becomes the shortest path for a selected
workload or authorized hardware capture is available; it must not be guessed.

### Linux defense in depth

- pidfd-backed worker lifetime/signalling (#297) is complete and merged;
- optional Landlock ambient-resource confinement (#298) remains open.

Landlock evaluation is non-blocking hardening and does not block C1 research
or the existing production diagnostic.

### Repository governance (#168)

Main protection/rulesets and merged-branch cleanup remain repository-admin
work. They are important process hardening but not emulator critical-path
semantics.

## Explicit non-claims

Astraea does **not** currently claim:

- commercial title boot or playability;
- a complete PS5 process-entry ABI;
- Windows retail admission;
- a complete hostile-code sandbox;
- complete PS5 HLE/LLE, GPU, shader, descriptor, surface-layout, or
  synchronization semantics;
- complete `sceAgcLinkShaders` output.

## Next action

Work #300. Encode the now-corroborated partial C1A direct-title prefix while
keeping the current `unsupported_initial_process_abi` production stop. Then
resolve RSI teardown, initial RSP, TLS/TCB and bootstrap ordering before any
retail instruction is admitted.

Use `docs/research/ps5_initial_process_abi.md` as the durable evidence record.
