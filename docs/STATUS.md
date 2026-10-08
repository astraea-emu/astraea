# Project Status

**Repository:** `astraea-emu/astraea`  
**Merged frontier:** production Linux x86-64 retail diagnostic path is active  
**Current critical path:** #300 — complete the evidenced PS5 initial-process contract before retail native entry  
**Graphics:** V0-V3 complete for their bounded owned workloads  
**Compatibility:** C0 complete on Linux x86-64; partial C1A prefix merged; C1 active  
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

1. Complete #334: prepare one exact pinned clean-room observer artifact, preserve
   its provenance/hashes, and run that **same artifact twice**.
2. Analyze both records offline with the merged C1A/C1B tooling. Promote only
   repeatable selected-profile facts.
3. Apply #333 and #335 to the exact selected entry path. If TLS/TCB or a
   bootstrap effect is not observable before the first controlled stop, defer
   it to C2 instead of blocking C1.
4. Freeze the first executable selected-process profile. Any **unknown required**
   field blocks admission; a field proved irrelevant before the selected stop
   does not.
5. Execute the real title entry under the supervised Linux runtime.
6. Record #350 — **C2.0 First Retail Divergence** — by reproducing the first
   typed deterministic post-entry boundary twice.
7. From that point onward, let the C2.0 boundary choose the next generic
   implementation slice.

The portable `astraea profile <artifact>` surface is merged and may be used in
parallel to compare lawfully owned first-title candidates by independent
structural dimensions. It shares artifact-reading and planning-stack policy
with the production diagnostic and never executes guest code.

Keep `unsupported_initial_process_abi` in production until the selected
profile contains no unknown required pre-entry state.

After C1, select the first lawful retail title by closure cost rather than
prestige and start S1 first-divergence/coverage accounting immediately.

Use `docs/research/ps5_initial_process_abi.md` as the durable evidence record
and ADR 0012 for scale/readiness strategy.
