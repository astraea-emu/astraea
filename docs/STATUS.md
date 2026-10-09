# Project Status

**Repository:** `astraea-emu/astraea`  
**Merged frontier:** C0 Linux retail diagnostics, bounded V0-V3 graphics, and source-owned two-ELF supervised execution plus mapping-epoch/generation protections through PR #388  
**Current execution priority:** measured pinned public-title/PRX lexical closure and first independently evidenced missing dependency; then selected-title readiness. Preserve #334 normal PS5 entry ABI as a distinct hardware gate. #377 owned TLS encoding is not a PS5 TLS runtime  
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

The first inventory and owned-code linker slices are **merged** in #358–#372.
The project now has exact checked public-title names, import symbol identities,
relocation reference/demand counts, an explicit owned module dependency graph,
bounded GLOB_DAT/JUMP_SLOT and RELATIVE patch encoders, and owned mapped-memory
application with full-target preflight and overlap rejection. None of these
implies PS5 system-library behavior or permission to execute commercial titles.

A **second source-generated, non-Sony PRX** from the same exact reviewed
BlackBear checkout is also a pinned CI corpus (#371, merged). It exposes
2,669 dynamic symbols, 1,896 relocations, 384 initialized TLS bytes and
1,128 total TLS bytes. The reproducible histogram is 1,790 RELATIVE,
100 JUMP_SLOT, 3 GLOB_DAT and 3 DTPMOD64 (TLS module ID), with 103 distinct
referenced symbol indices. See
`docs/research/public_cleanroom_prx_second_corpus_2026-10-08.md`.
The exact relocation-class CI ratchet (#373) and checked owned RELATIVE
application (#374) are **merged**. The explicit source-owned TLS module-ID encoder was reconciled from
superseded #375 into #377, **merged** after five exact-head checks.
It encodes an **externally assigned owned runtime module index**, not a PS5
TLS loader or a way to infer runtime module IDs. No TLS runtime module assignment
or Sony process-entry evidence follows from this source-owned encoder.

This work remains strictly PC-side. Unrecognized Sony runtime modules and
HLE behavior remain unsupported; no fake-success stubs, guessed ABI fields
or proprietary executable/firmware assets are added.

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

## 2026-10-09 source-owned execution and public-ELF closure checkpoint

- PRs #379–#388 progressively merged **source-authored** independent
  client/provider ELF validation, exact graph-resolved callable relocation,
  in-process and separately supervised native guest execution, byte-identical
  sealed worker inputs, fail-closed typed rejection, SHA-256 source baselines,
  target preflight/overlap checks, explicit provider retirement, generation
  identities and prepared-memory ownership epochs. The latest #388 merge is
  3a4e06038792a48163a5e9291b22b6383510f747, with Linux x64,
  Linux sanitizers, Windows x64, macOS ARM64 and fuzz-smoke checks green.
  This remains an opt-in owned research profile, not a Sony module loader.
- The pinned public source-build run
  https://github.com/astraea-emu/astraea/actions/runs/37982894151
  executed a new *read-only* source-built title versus clean-room PRX lexical
  import-demand comparison in PR #389: **25** relocation-referenced external
  title identities, **2,566** defined global/weak long-form PRX export rows,
  **0 exact literal NID/library/module triples**, **8 NID-only lexical
  candidates**, and **17 without an equal NID**. No binding/relocation was
  attempted; guest instruction count was **zero**. The accompanying
  analyzer's self-test also passed. This pinned workflow result alone does not
  waive the separate required five-platform merge checks for PR #389.
- See docs/research/public_pinned_title_prx_lexical_closure_2026-10-09.md
  for the exact source/artifact hashes and boundary. **Even an equal NID is
  not a compatible service or a verified defining module**: the encoded
  library/module fields are not known to be globally scoped across binaries.
- Production retail diagnostics remain fail-closed at the independently
  observed dependency / process-entry boundary. No commercial title has
  executed its first guest instruction, drawn a real-title frame or booted.
  The real normal-title PS5 observer evidence in #334 remains uncollected.

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

## 2026-10-09 execution map

[PC-first C1 execution and reference-console readiness](research/c1_pc_first_execution_hardware_readiness_2026-10-09.md) is the scoped next-task and hardware-gate checklist. Prioritize [#378](https://github.com/astraea-emu/astraea/issues/378): an **end-to-end owned two-module supervised guest execution route**, not another standalone patch encoder. Only after that, prioritize lawful selected-title structural preflight and the irreducible #334 startup observation. Preserve #334 open until two authentic normal-title entry observations are collected.

## Next action

1. Preserve the complete pinned public-title/raw-PRX source and report hashes,
   25-reference demand census, and observed **0 exact / 8 NID-only /
   17 no-NID** lexical classes as a strict, read-only regression.
   Do not turn eight shared NIDs into guessed Sony provider associations.
2. Identify the **first function/dependency actually required during this
   independently authored title's startup**, then test exact provider identity
   and defining-module authority against source evidence. A host-only lexical
   match is insufficient to bind, relocate, or call an export.
3. Reuse the source-owned two-ELF supervised execution and mapping-lifetime
   primitives where the observed dependency warrants them; finish #378's
   remaining integrated contract with explicit distinction between a
   source-owned proof and Sony runtime semantics. No blanket HLE success
   stubs, speculative concurrent unload, or invented TLS module IDs.
4. Preserve the pinned public relocation inventories and #377's explicit
   DTPMOD64 encoder. The PRX's TLS module-ID allocation and Sony process
   ABI remain **unverified** even though standard AMD64 bytes can be encoded.
5. Select a lawfully held, low-complexity commercial-title executable for
   **read-only** static demand/provenance assessment when authorized bytes
   are available; store hashes/normalized reports, never copyrighted bytes
   in the repository. One public homebrew title is not a retail game.
6. Activate #334's two authentic same-title normal-entry PS5 observations
   only for the earliest otherwise irreducible ABI blockers, keeping the
   reference console firmware untouched until then. Preserve #348's
   unknown-required default-deny and let the real first failure choose #350
   and downstream runtime/HLE/graphics work.

**Long-term:** C3 sustained boot → C4 genuine title-generated GPU work →
C5 interactive menu → C6A in-game → C6B defined playable route → C6C
regression-guarded, independently reference-validated support. Astraea
Verify remains a proposed product advantage, not a demonstrated feature.

Full governing milestones: `docs/PROJECT_PLAN.md`. PS5 hardware evidence,
commercial guest instruction execution, and gameplay are still absent.
