# Project Status

**Repository:** `astraea-emu/astraea`  
**Merged frontier:** production Linux x86-64 retail diagnostic path is active  
**Current execution priority:** #356 — PC-only public native-title dependency identities; #334 hardware ABI retained for later C1 promotion  
**Graphics:** V0-V3 complete for their bounded owned workloads  
**Compatibility:** C0 complete on Linux x86-64; partial C1A prefix merged; C1 active  
**CI merge gate:** Linux x64, Windows x64, macOS ARM64, Linux ASan+UBSan, Linux Clang fuzz smoke; pinned C1 workflow also required when its source/workflow changes

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

## 2026-10-08 software-first checkpoint

The public, independently authored
`blackbearreloaded/ps5-native-app-boilerplate@2f672d1c` title is now
built and tested in the pinned CI integration workflow (PRs #353–#355):

- `astraea profile`: 14 program headers, 5 load segments, 4 generic needed
  entries, 4 SCE needed-module entries, 4 SCE import libraries, 40 RELA
  relocations, and a zero-byte TLS template/header;
- `astraea diagnose`: stable pre-entry typed refusal
  `unsupported_dynamic_dependencies`, `guest_rip=0x10`,
  `detail0=12` (sum of metadata entry categories, **not** 12 unique modules);
- real ELF-pair offline analyzer and synthetic repeat/disagreement tests pass.

This is **neither a retail game nor PS5 hardware observation**. It shows
a genuine PC-testable closure dependency earlier than the initial-process
ABI on this particular public artifact. See
`docs/research/public_native_title_closure_2026-10-08.md`.

Current work queue: **#356**, deterministic fail-closed module/import
identity inventory from existing validated loader metadata; then a scoped,
generic module/linker and relocation test slice only when independently
reproducible. No fake-success HLE, invented ABI fields, speculative GPU
breadth or copyrighted executable assets.

The normal-title #334 two-run hardware observation is **open and unfulfilled**.
It was corrected after an unsupported "completed" issue closure on
2026-10-08. Keep firmware-specific hardware testing until software-side
preflight/first-title readiness is established. The physical experiment
must use the normal title-entry path, not the exploit payload entry;
contributor permission, firmware provenance and console backups remain
separate responsibilities.

The C/V/S milestone definitions are unchanged. See the new
`C1 software-first execution sequence` under `docs/PROJECT_PLAN.md`.
Unknown load-bearing hardware state still prevents retail native entry.

## Current critical path: C1

Issue #300 asks for the smallest evidenced PS5 initial-process contract needed
before retail native entry.

Promote it in layers rather than as one guessed ABI:

1. **C1A — entry register/parameter-block contract**
   - corroborated RDI/startup-block prefix is merged (#304);
   - process-entry validator is merged (#308);
   - preserving pre-CRT observer is merged (#325);
   - pinned fail-closed native-title preparation is merged (#346);
   - remaining work is the two-run #334 hardware observation for RSI/RSP.
2. **C1B — process metadata**
   - startup vector and static `PT_SCE_PROCPARAM` are already distinct;
   - post-init observation + offline identity analyzer are merged (#327);
   - #334's same two runs decide the selected-profile
     `sceKernelGetProcParam()` relationship.
3. **C1C — primary-thread TLS/TCB**
   - external optional FS/GS sidecar support is merged (#339);
   - #333 decides whether TLS/TCB is even observable before the selected first
     controlled stop; if not, defer it to C2.
4. **C1D — pre-entry bootstrap effects**
   - #335 admits only effects actually consumed before the selected first
     controlled stop; all continuing runtime initialization belongs to C2.

Dynamic TLS, additional guest threads, runtime module loads and continuing
initialization after the first admitted instruction belong to C2 unless a
selected workload proves they are pre-entry requirements.

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

The software/tooling side of C1A and C1B is now merged, including the
pre-CRT observer, same-run procparam analyzer, repeat comparator, and pinned
external-checkout preparation. The remaining hard gate is the #334 controlled
two-run observation, followed by selected-path decisions for #333/#335 rather
than universal TLS/bootstrap reconstruction.

## Scalability / readiness axis

ADR 0012 adds a third orthogonal S0-S5 axis so compatibility breadth does not
outgrow verification and architecture:

- **S0** fail-visible verification/provenance — established;
- **S1** first-divergence and workload coverage accounting — begin with real
  title execution;
- **S2** lawful local cross-title routes/regression guards — grow after visible
  milestones;
- **S3** architecture ratchets — add when real compatibility-debt classes
  appear;
- **S4** measured performance budgets — after representative 3D workloads;
- **S5** release/user-quality readiness — late.

C6 is refined to C6A in-game, C6B playable defined route, and C6C
reference-validated/regression-guarded support.

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
C0 diagnostic -> C1 first retail instruction
 -> C2.0 first deterministic post-entry divergence (#350)
 -> C2 post-entry runtime closure -> C3 boot
 -> C4 first headless title GPU/frame evidence
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

### Windows retail parity (#320)

Windows x64 retains owned native/supervisor proofs but not arbitrary retail
admission. The parity gate requires a default-deny boundary proving that raw
guest `SYSCALL` cannot reach the Windows kernel, plus equivalent artifact/
resource/IPC containment. This is not on the Linux C1 critical path.
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

1. **#356:** derive a bounded manifest of exact generic/SCE module and
   import identities from the public native-title ELF via existing
   `GuestImage` / initialized-image string reader. Report declared,
   unique, unresolved and malformed items separately; test under hostile
   synthetic inputs and the pinned CI title. No invented Sony semantics.
2. If #356 demonstrates a genuine generic pre-entry requirement, split the
   minimum typed module graph and relocation/link-resolution test slice.
   Resolve only independently modeled interfaces; fail closed on unknown
   calls/objects. Keep the production supervisor boundary unchanged.
3. Profile one lawfully held primary commercial-title executable and two
   reserve candidates when artifacts are legally available. Rank independent
   closure dimensions, not a fictitious percent complete. Store only lawful
   normalized results and hashes.
4. Keep the observer build, offline analyzer, repeat comparison and
   cross-host CI green. Fix any real defect found by these tests.
5. **Then**, when remaining selected-path facts are irreducibly hardware
   dependent, perform #334's same-artifact two-run normal-title entry
   observation using authorized reference hardware. Preserve unchanged
   firmware, toolchain, hashes and two raw records; no assumption that
   exploit-loader execution is title entry.
6. Classify #312/#333/#335 on the bounded selected path, implement #348
   only with evidence and keep `unknown_required` as a hard refusal.
7. Complete C1 native real-title entry under the Linux worker and repeat
   #350's C2.0 first typed post-entry divergence twice. Then let actual
   C2 runtime/HLE/GPU behavior select the next generic patch.

**Long-term product:** C3 boot → C4 real-title GPU evidence → C5 visible
interaction → C6A in-game → C6B playable defined route → C6C
reference-validated, guarded support, while S1–S5 scale regression,
performance and user readiness. Proposed Astraea Verify diagnostics are
not yet a demonstrated differentiator.

Full governing milestones: `docs/PROJECT_PLAN.md`. Hardware evidence
remains pending; do not claim PS5 boot, gameplay or emulator superiority.
