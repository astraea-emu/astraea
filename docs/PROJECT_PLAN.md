# Astraea Project Plan

## 1. Objective

Build a clean-room, verification-first PlayStation 5 compatibility research
and emulation project whose correctness is driven by public specifications,
controlled experiments, deterministic tests, and regression localization
rather than title-specific hacks.

The long-term quality bar is multi-dimensional:

- **correctness** — evidenced guest-visible semantics and explicit unknowns;
- **compatibility** — broad cross-engine title progression without core hacks;
- **performance** — measured CPU/GPU efficiency on representative workloads;
- **reliability** — deterministic diagnostics and regression-resistant routes;
- **user quality** — stable presentation, input/audio/save behavior and
  reproducible releases once the core is ready.

Efficiency means removing the **first real dependency** on the shortest
end-to-end path, not maximizing opcode count, HLE surface area, compatibility
counts, screenshots, or speculative feature breadth.

## 2. Source of truth

GitHub is authoritative.

- `docs/PROJECT_PLAN.md` — durable architecture and gate definitions.
- `docs/STATUS.md` — concise merged frontier and exact next action.
- Open issues / pull requests — in-flight work.
- `docs/adr/` — durable architecture decisions.
- `docs/research/` — evidence and unresolved questions.
- `docs/specs/` — narrow behavioral/data contracts.
- CI — mechanical acceptance gate.
- Chats — working sessions only.

A merged implementation outranks a stale document. When a discrepancy is
found, fix the document rather than preserving obsolete status language.

## 3. Core principles

1. **Evidence before PS5-specific behavior.**
2. **Clean-room provenance.** No Sony firmware, keys, proprietary SDK files,
   decrypted retail content, proprietary system modules, or unauthorized
   redistribution.
3. **Guest semantics before host mapping.**
4. **Unknown means unknown.** Unsupported behavior is typed and observable.
5. **Portable core, specialized runtime.**
6. **Verification first.** Parsers, state, IR, runtime boundaries, and
   experiments must be testable and traceable.
7. **Dependency-driven vertical integration.**
8. **Small reviewed PRs with explicit acceptance and forbidden scope.**
9. **Separate verified implementation from provisional research** (ADR 0008).
10. **Keep guest GPU identity/layout separate from host resources** (ADR 0009).
11. **Treat retail code as untrusted** (ADR 0010).
12. **Do not confuse compatibility progress with semantic correctness.**
13. **Direct-title first.** Full firmware/VSH boot is not a prerequisite unless
    a real dependency proves otherwise.
14. **Expand breadth on demand.** HLE, shader ISA, resource caches, scheduler,
    services, and UI grow when an owned or retail workload requires them.

## 4. Architectural layers

### A. Loader / process image

- ELF/SCE structural validation
- program mappings and permissions
- dynamic metadata
- relocations
- module/library/import/export identity
- process parameters
- initial stack
- TLS/TCB metadata and runtime placement

### B. Guest process / CPU runtime

- guest address space
- guest CPU context
- native x86-64 transition where appropriate
- guest process/thread/handle identity
- fault capture
- HLE/syscall dispatch
- controlled stop/resume

### C. Supervised retail runtime

- controller/worker isolation
- sealed artifact authority
- bounded protocol
- syscall interception before host-kernel fallthrough
- host-resource policy
- finite execution/resource budgets
- deterministic diagnostic/fault reporting

### D. PS5 platform services

- exact import identity
- module/runtime-linker policy
- memory/process/thread services
- synchronization/events/time
- filesystem/input/audio/video
- other services pulled by real workloads

HLE is not a finish-all phase. Implement only services needed by the next
verified workload.

### E. PS5 GPU frontend

- AGC shader containers/objects
- submitted PM4/command streams
- register/state model
- guest GPU allocations/resources/views
- surface layouts
- descriptors
- synchronization
- presentation state

Raw provenance remains available alongside typed semantics.

### F. Shader semantics

```text
RDNA2 bytes
 -> decoded instructions
 -> semantic Shader IR
 -> semantic/oracle execution where useful
```

The semantic layer represents guest behavior, not Vulkan convenience.

### G. Shader compiler

```text
semantic Shader IR
 -> workload-driven compiler/value IR
 -> structured/lowered compiler form
 -> SPIR-V
```

The compiler IR grows only when real shaders require SSA/value flow, phis,
structured control flow, resource discovery/lowering, stage I/O, or
optimization passes. Do not turn semantic Shader IR into a host compiler IR.

### H. Host GPU backend

- Vulkan resource materialization
- pipeline/shader module creation
- synchronization mapping
- headless execution/readback
- eventually presentation

Vulkan is a backend, not Astraea's guest specification.

### I. Verification infrastructure

- Trace v0 and first-divergence diffing
- AstraeaProbe v0
- deterministic fixtures
- fuzzing and sanitizers
- controlled reference-hardware evidence
- compatibility/diagnostic reports without proprietary bytes

## 5. Host strategy

### Development

macOS, Linux, and Windows remain first-class for portable code and CI.

### Native guest execution

The PS5 CPU is x86-64.

- Linux x86-64: native owned probes plus production retail diagnostics.
- Windows x86-64: owned native/supervisor proofs exist, but arbitrary retail
  admission remains disabled until equivalent pre-kernel syscall containment
  exists. #320 is the parity lane; concrete retail admission should begin from
  a stable Linux C2/C3 workload unless an independent bounded containment proof
  can land earlier without delaying C1.
- Apple Silicon macOS: portable analysis/compiler/test host, not native PS5 CPU
  execution. A future CPU-translation strategy would be a separate major
  decision, not an implicit extension of the native backend.

### Graphics

Vulkan is the first host graphics backend; SPIR-V is the first backend shader
format.

## 6. Foundation milestones

### M0 — engineering foundation — complete

Build system, CI, warnings, sanitizers, fuzzing, provenance, ADR discipline.

### M1 — validated guest image — complete

Strict ELF/SCE parsing, mappings, dynamic/relocation/import foundations,
bounded untrusted-input handling.

### M2 — controlled owned execution — complete

Owned synthetic x86-64 guest entry, HLE boundaries, deterministic exit/fault
behavior on supported x86-64 hosts.

### M3 — verification infrastructure — established

Trace v0, diffing, AstraeaProbe, evidence maps, reproducible experiments.

These remain evolving infrastructure, not a waterfall.

## 7. Graphics vertical gates

Graphics gates measure bounded technical integration. They do not measure game
compatibility.

### V0 — PS5 shader ingestion — complete

```text
AGC container
 -> validated PS5 envelope
 -> bounded RDNA2 stream
 -> semantic Shader IR
```

### V1 — guest-created shader identity — complete

```text
owned SCE guest
 -> exact import/HLE
 -> sceAgcCreateShader
 -> validated prepared shader
 -> persistent stage-aware created-shader identity
```

### V2 — validated host shader module — complete

```text
supported semantic Shader IR
 -> compiler/value lowering
 -> deterministic Vulkan-valid SPIR-V
```

### V3 — submitted guest GPU workload -> Vulkan result — complete

Bounded completion proof:

```text
owned submitted PM4/DCB bytes
 -> draw-time SH/CX/UCONFIG state
 -> stage-qualified created-shader identity
 -> stored semantic Shader IR
 -> compiler/value IR
 -> generated vertex/fragment SPIR-V
 -> submitted typed guest color target
 -> guest allocation/image resolution
 -> real Vulkan raster
 -> exact deterministic readback
```

This proves architecture composition. It does **not** prove complete PS5 PM4,
shader ISA, descriptor, texture, tiling/compression, synchronization, or
graphics compatibility.

### V4 — controlled PS5 differential — cross-cutting validation gate

When authorized hardware access is available and a specific behavior requires
it, run the same owned workload on reference hardware and Astraea and compare
normalized state/output.

Targeted hardware probes may occur before or after other V-gates if they are
the shortest way to answer one bounded evidence question.

### V5 — presentation — future

```text
guest GPU result
 -> guest synchronization
 -> VideoOut / flip model
 -> host presentation
```

Headless correctness remains independently testable.

## 8. Retail compatibility ladder

Compatibility gates are orthogonal to V0-V5.

### C0 — production retail diagnostic — complete on Linux x86-64

```text
user-selected artifact
 -> controller bounded read
 -> sealed artifact handoff
 -> supervised worker
 -> resource policy
 -> deterministic PS5/SCE preflight
 -> typed diagnostic/fault result
```

The Linux runtime also has a verified pre-kernel seccomp boundary for admitted
guest-originated syscall entry.

C0 permits a legally obtained retail executable to be used as an **untrusted
diagnostic input**. It does not imply that any retail instruction should be
executed yet.

### C1 — evidenced PS5 process-entry ABI / first retail instruction — active

C1 is the current critical path (#300). **The execution order is software-first:
retire independently testable closure dependencies before scheduling the
reference-console observation (#334).** No completed PC-only task substitutes
for actual PS5 hardware entry evidence.

### C1 software-first execution sequence — 2026-10-08

This is an execution order **within C1**, not a new architectural gate or
permission to relax #348's ABI admission rules.

1. **Frozen public input (complete):** PR #355 compiles an independently
   generated, GPL-3.0-or-later PS5-native title at pinned
   `blackbearreloaded/ps5-native-app-boilerplate@2f672d1c`.
   `astraea profile` succeeds; `astraea diagnose` safely stops at
   `unsupported_dynamic_dependencies`. The input has four generic
   `DT_NEEDED` records, four SCE needed-module records, four SCE
   import-library records, forty RELA relocations and a zero-byte TLS
   template/header. **The diagnostic's 12 records are not 12 distinct
   dependency modules; no guest instruction ran.** Reproduce this in CI;
   see `docs/research/public_native_title_closure_2026-10-08.md`.
2. **Dependency identity (#356; first implementation):** using the existing
   validated `GuestImage`, string bounds checking and SCE metadata,
   classify exact raw module/library/import identities and unresolved
   references. Preserve entry/source indices, duplicates and uncertainty.
   Reject malformed strings; escape untrusted terminal output. Distinguish
   declared, uniquely identified, resolved, called and semantically correct.
   Acceptance: repeatable, bounded local manifest from the pinned public
   image plus malformed/synthetic negative fixtures; no private artifacts.
3. **Link/relocation closure (conditional follow-on):** only after the
   manifest is accepted, introduce the smallest generic `ModuleGraph`,
   mapping lifetime and HLE-vs-LLE resolution contracts that can be tested
   with independently authored modules. Make strong/weak unresolved imports,
   version/identity conflicts, relocation failures and initialization
   ordering explicit. A missing PS5 library is *not* automatically resolved
   by inventing an address or returning zero from an HLE.
4. **Owned execution experiments (optional and isolated):** the supervisor
   may run independently authored workloads under explicitly labelled
   synthetic/research-only process state to validate safe transitions,
   import dispatch and first-stop classification. This is a test harness,
   not verified retail ABI emulation; it never changes production retail
   admission and never counts as a C1 title-instruction milestone.
5. **First-title and launch-readiness check:** select one lawful primary
   retail workload using comparable closure profiles, with two reserve
   workloads from meaningfully different engines. Establish its static
   dependency graph, mapped entry, first bounded control-flow slice,
   needed metadata and an exact-build reproducibility record.
6. **Reference hardware only for irreducible unknowns (#334):** on an
   authorized normal native-title PS5 environment, preserve one exact
   observer binary and collect two complete pre-CRT observations, hashes,
   toolchain/firmware/launcher provenance, per-run JSON and structural
   comparison. A payload-loader entry or public emulator startup guess
   is not equivalent. The research console remains untouched until
   software closure and the native-title launch/log path are ready.
7. **Hardware-informed entry (#312/#333/#335/#348) then C2.0 (#350):**
   classify every selected-path load-bearing entry field as required,
   irrelevant_before_stop or unknown_required. Admit no unknown required
   state. Run a lawful real-title entry in the Linux supervisor and
   reproduce the first typed post-entry divergence twice.

**Sequencing rule:** steps 2–5 may occur in parallel with research, but do
not turn speculative services, giant SDK catalogs or general GPU work into
the critical path. Schedule step 6 only when it would resolve the earliest
remaining selected-path uncertainty. Every accepted generic fix needs a
reproducer, negative case, source provenance and exact-head CI.

**2026-10-10 integration checkpoint (PRs #397–#400):** Generic owned proofs now include assembled guest code, an independently host-linked ELF under the Linux supervisor, and two independently linked generic ELFs transferring through a linker-generated `JUMP_SLOT`. The separately source-built minimal **PS5-format** ELF has no imports but eight `R_X86_64_RELATIVE` records and stops at `unsupported_relocations` without entering guest code. The next *research-only* end-to-end gate is checked mapping/rebasing and atomic relative-patch preflight/application on that same selected PS5-format image, followed by an intentional `UD2` first-instruction fault in two sealed worker launches. Retain the production PS5 entry-ABI gate and the larger public-title negative corpus; this checkpoint does not satisfy commercial-title C1.

Promote the actual initial-process ABI in layers:

#### C1A — entry registers and parameter block

The independently corroborated startup prefix, transport-neutral validator,
preserving pre-CRT observer, repeat comparator, and pinned fail-closed external
native-title preparation are merged.

Promoted prefix:

- RDI / first SysV argument identifies the loader-built startup block;
- argc-like 32-bit field at +0;
- argv-like pointer vector begins at +8;
- the original startup-block pointer reaches runtime environment
  initialization.

Remaining C1A evidence is intentionally narrow: #334 runs one exact prepared
owned title artifact twice to establish the selected-profile RSI/RSP behavior.
Do not manufacture those fields from comparative emulator behavior.

#### C1B — process metadata

The loader-built startup vector and static ELF `PT_SCE_PROCPARAM` are
separate concepts. The post-init observer and offline identity analyzer are
merged; #334's same two runs establish the selected-profile relationship among
the startup vector, `sceKernelGetProcParam()`, and mapped
`PT_SCE_PROCPARAM`.

#### C1C — primary-thread TLS/TCB

C1C is a **selected-path dependency**, not a universal prerequisite. Inspect
the exact path from entry to Astraea's first controlled stop.

If that path cannot observe FS/GS/TLS/TCB, mark those facts irrelevant before
the selected stop and defer them to C2. If it does observe them, measure only
the exact first required relationship. Optional external FS/GS sidecar support
is already available without coupling Astraea to debugger transport.

Dynamic TLS, additional guest threads, TLS module growth and runtime thread
lifecycle belong to C2 unless the selected path proves otherwise.

#### C1D — pre-entry bootstrap effects

C1D is also selected-path driven. Promote only module/import/runtime effects
actually consumed before the selected first controlled stop.

C1D does not require implementing every runtime module, `_init_env`,
constructors, dynamic TLS, later threads, or continuing initialization merely
because they are startup-adjacent. Those first post-entry dependencies define
C2 unless the selected path proves otherwise.

**C1 completion criterion:** one selected legally obtained or independently
owned title/profile may execute its first native retail instructions without
using Astraea's synthetic owned-probe stack and without inventing unknown entry
state.

### C2.0 — first deterministic post-entry retail divergence

Immediately after C1, run the selected title until the first **typed,
reproducible post-entry boundary** (#350).

C2.0 does not require boot or a minimum instruction count. It requires:

- real title entry through the evidenced selected profile;
- guest RIP/context for the first post-entry stop;
- a typed boundary/fault classification;
- repeat-run agreement on the normalized first boundary.

That boundary chooses the first C2 implementation slice.

### C2 — post-entry runtime closure

Advance from C2.0 through the real post-entry dependencies one at a time:

- module graph / runtime linker;
- relocation/import completion beyond the pre-entry minimum;
- HLE-vs-LLE module policy;
- dynamic TLS and guest thread lifecycle when first required;
- first guest syscall/HLE services;
- process/thread/handle model;
- runtime module loads / init calls required after entry;
- deterministic first unsupported runtime boundary

C2 is workload-driven. Do not pre-implement an entire OS API catalog.

### C3 — deterministic boot / sustained initialization

A title reaches a stable, reproducible initialization milestone beyond isolated
startup calls.

Define this per selected workload before implementation. "Boot" must mean more
than "the executable mapped" or "one instruction ran."

### C4 — first real-title headless GPU/frame evidence

A real title reaches a guest GPU submission that Astraea can interpret through
the verified frontend/backend and produces deterministic headless evidence.

This is where page tracking, buffer/image caches, pipeline/shader caches,
descriptor breadth, scheduler/synchronization, and broader shader compiler
passes are likely to become load-bearing.

### C5 — visible presentation / menu

Real title output reaches VideoOut/presentation and a defined visible
boot/menu milestone.

### C6 — in-game / playable / validated support progression

C6 is deliberately subdivided:

#### C6A — in-game progression

A defined route reaches recognizable gameplay with the scene rendering. Severe
visual, timing, audio, input, save or performance defects may still exist.

#### C6B — playable defined route

A reviewed route can be completed with the behavior required by that route,
including the relevant input/audio/save/presentation path. This is still not a
global compatibility claim.

#### C6C — reference-validated and regression-guarded support

The selected route/state has been compared against an authorized reference or
another independently justified oracle where needed, and an appropriate
automatic local regression guard protects the supported milestone.

Compatibility reporting must distinguish at least:

- diagnostic
- native-entry
- boot
- first-frame/headless-GPU
- menu
- in-game
- playable
- reference-validated / guarded

A category is an integration observation, not proof of complete platform
semantics.

## 8.1. Scalability / readiness gates

The S-axis is orthogonal to both graphics technology and compatibility. It
measures whether Astraea can grow from one successful title to a durable
emulator without accumulating unmeasured compatibility debt.

### S0 — fail-visible verification and provenance — established

Maintain exact-head CI, typed unsupported behavior, sanitizers/fuzzing,
deterministic owned fixtures, AstraeaProbe/trace support, and durable
evidence/ADR records.

### S1 — first-divergence and coverage accounting

Once C1 admits real-title execution, measure the selected workload:

- modules/imports discovered;
- imports resolved;
- imports actually called;
- semantically implemented called HLEs;
- first unsupported CPU/syscall/HLE boundary;
- AGC/PM4 packet kinds observed;
- shader instructions decoded and lowered;
- first unsupported shader operation;
- resource/synchronization classes encountered.

Do not use a guessed platform-wide completion percentage. Retail bytes remain
local; commit only lawful metadata, digests, normalized counts and typed
boundaries.

### S2 — local cross-title routes and regression guards

As visible milestones appear, build a lawful local compatibility corpus spanning
at least:

1. small/custom 2D;
2. Unity/IL2CPP;
3. Unreal;
4. demanding custom 3D.

A shared-subsystem compatibility change should be exercised against the
available guarded corpus before merge when practical. Dumps, screenshots and
copyrighted title assets remain local/gitignored.

### S3 — architecture ratchets

When a real compatibility-debt class appears, make it mechanically non-growing.

Candidate ratchets:

- title IDs / executable hashes / shader hashes in shared core code;
- unregistered title-specific workarounds;
- unknown HLE success without evidenced semantics;
- host exceptions crossing guest frames;
- behavior-changing untyped environment switches;
- steady-state blocking GPU waits/readbacks;
- unbounded per-draw/per-HLE hot-path allocations;
- production SPIR-V emitters without validation coverage.

Do not add a ratchet until it can distinguish a real violation from legitimate
code.

### S4 — measured performance budgets

Profile representative 3D workloads before optimizing. Track CPU/HLE/render
phase time, GPU submit/render/present time, waits/readbacks, shader/pipeline
compile events, hot-path allocation volume, frame progression/stalls, and
residency/cache pressure when relevant.

Correctness remains authoritative. A faster wrong result is not progress.

### S5 — release and user-quality readiness

Only after the title path is durable, add release-grade packaging, crash/
diagnostic bundles, stable configuration, controller/input, audio, save-data
and presentation quality, documented host/GPU/driver support, exact-build
compatibility reports, and state migration where needed.

See ADR 0012.

## 9. Direct-title-first strategy

Astraea will not make full firmware/VSH boot the default critical path.

Why:

- direct-title diagnostics expose missing loader/runtime/HLE/GPU dependencies
  sooner;
- firmware boot introduces a much larger system-service surface before a title
  can provide useful signal;
- PS4/PS5 emulator projects demonstrate that VSH/firmware boot is itself a
  major product-sized track.

Firmware/VSH research remains optional if a selected dependency later
justifies it.

## 10. First-title selection and workload-driven expansion after C1

### First retail target

Select the first legally obtained retail workload for **closure cost**, not
prestige.

Prefer:

- small module/import surface;
- minimal network/entitlement dependence;
- deterministic startup route;
- modest shader/resource complexity;
- independently demonstrated feasibility in public implementations;
- usefulness as a representative engine/workload class.

Do not choose a difficult AAA title merely because it is impressive if a
smaller workload can expose the same runtime dependency faster.

After one low-complexity title is progressing, expand toward the S2 corpus
rather than optimizing one title indefinitely.

### Dependency-driven expansion

When a real title reaches a missing subsystem, implement the smallest durable
abstraction rather than a title patch.

Expected later abstractions include:

### Runtime/module layer

A `ModuleGraph` / runtime-linker layer above `GuestImage`:

- loaded module identity/lifetime
- library/NID resolution
- HLE/LLE resolution policy
- dependency ordering
- relocations across modules
- unload/reload when later required

Measure imported -> resolved -> called -> semantically implemented interfaces
for the selected workload. A missing/unknown call must stay visible; do not
return success while leaving required outputs undefined merely to advance a
title.

As the called surface grows, introduce declarative ABI/signature metadata and
per-library conformance tests rather than hand-maintained ad-hoc dispatch.

Do not turn the structural ELF parser into a runtime linker.

### Host CPU ISA compatibility layer

Native x86-64 execution is a performance strategy, not a claim that every host
implements the PS5 Zen 2 instruction surface identically.

When a selected title first reaches an unsupported host instruction:

- record the exact guest opcode/fault and host capability;
- prefer a bounded ahead-of-execution patch/trampoline when it preserves
  semantics cleanly;
- otherwise trap and emulate the smallest instruction family required;
- test the native-supported and compatibility paths against one semantic
  oracle;
- keep host ISA compatibility separate from PS5 OS/HLE semantics.

Current public PS5 projects already need this class for AMD-specific/SHA and
other host-dependent instructions, especially across Intel and Rosetta
environments. This is an expected post-C1 dependency class, not work to
pre-implement before a selected title exposes it.
### Guest process/kernel layer

Introduce typed process/thread/handle abstractions when demanded:

- guest PID/TID identity
- per-thread CPU/TLS state
- handle/object table
- waits/events/semaphores/mutexes
- virtual memory lifecycle
- clocks/timers

Do not expose unmanaged host thread/process identity as guest semantics.

### GPU residency/cache layer

Once real title GPU workloads require it:

- canonical guest resource identity;
- guest memory page tracking / invalidation;
- explicit CPU-dirty / GPU-dirty ownership where needed;
- buffer and image caches;
- alias tracking;
- pipeline/shader cache;
- descriptor/resource discovery;
- queue/scheduler/synchronization model.

Current PS5 projects show that resource identity, dirty-page visibility,
readback avoidance and synchronization become major correctness/performance
boundaries under real 3D workloads. That is strong evidence to reserve a clean
architecture seam, not permission to implement the subsystem before C4 makes
it load-bearing.

These are expected eventual needs, not current speculative tasks.

### Shader compiler maturity

When real shaders exceed the current bounded compiler profile:

- build a local histogram/coverage report over the selected lawful shader
  corpus and rank the first unsupported operation by impact;
- SSA/value flow;
- dominance/phi handling;
- structured control flow;
- resource discovery/lowering;
- stage I/O lowering;
- optimization passes;
- deterministic compiler cache keys.

Preserve the semantic Shader IR as the correctness/provenance layer.

Astraea already validates emitted SPIR-V in bounded tests. As production emitter
paths multiply, add a coverage guard so a new emitter cannot silently bypass
validation.

## 11. Independent evidence tracks

### LinkShaders (#191 / #207)

Complete guest-visible `sceAgcLinkShaders` only when the unknown returned tail
records are measured for the exact supported profile. Public compiler inputs
or plausible register values are not returned-output evidence.

This track advances when it becomes the first dependency of a selected
workload or authorized hardware capture is available.

### Linux defense in depth (#297 / #298)

pidfd-backed lifetime/signalling is merged. Landlock remains worthwhile
post-C0 hardening, not a blocker for #300.

### Repository governance (#168)

Protect `main`, enforce required checks mechanically, enable merged-branch
cleanup, and prune only verified merged branches.

## 12. Compatibility reporting

Once actual title diagnostics are run, record a normalized report without
retail bytes:

- title/artifact identity by lawful metadata + cryptographic digest
- Astraea commit
- host OS/CPU/GPU/driver
- diagnostic/compatibility category
- first unsupported boundary
- normalized trace/log identifiers
- reproducibility notes

Never commit game data, keys, decrypted system modules, or proprietary
artifacts merely to reproduce compatibility.

## 12.1. Competitive positioning and proof standard

The product goal is reliable, broadly compatible and performant PS5
emulation, not just an academically correct emulator or an impressive
single screenshot. Existing public competitors (SharpEmu, KytyPS5,
prosper and others) already demonstrate substantial real-title progression.
Astraea must first earn comparable *selected-title* functionality before
a user has a reason to choose it.

The proposed product differentiator is **Astraea Verify**: reproducible
compatibility reports that connect an exact emulator build, test route,
host CPU/GPU/driver, first typed divergence, frame/input progression,
and sanitized diagnostic evidence. It must remain a future product
hypothesis until it proves useful against existing competitor diagnostics.

Measure success through independently repeatable gates:

- first genuine retail guest instruction and repeatable first failure;
- generic blockers removed with zero regressions in verified routes;
- at least one deterministic visible/menu route, then a defined playable
  route, then a second independently selected engine;
- semantic correctness and explicitly unsupported calls (never default
  success from unknown service behavior);
- scene-specific p50/p95/99th-percentile frame times and rendering
  correctness once games produce representative frames;
- user-quality: reproducible installs, meaningful safe crash reports and
  clearly labelled support matrices, when S5 becomes active.

No compatibility percentages based only on import/shader counts, no
unsupported claims of superiority, and no compatibility score inferred
from other emulators' screenshots. Keep CPU/GPU performance claims tied
to measured comparable scenes, builds and hardware.

Use public emulator and independently console-measured research as an
**experiment queue and provenance map**, not an automatic code source.
Check licenses per file, distinguish measured behavior from source-level
assumptions, and never vendor no-license/GPLv2-only implementations into
Astraea's GPLv3-or-later tree absent permission and legal review. Prefer
official AMD/Khronos specifications, source-attributed open SDK interfaces,
and testable independently authored examples. Do not commit commercial
content, firmware, keys or private hardware artifacts.

## 13. Issue selection rule

After every meaningful merge:

1. Identify the active C- or V-gate.
2. Trace the shortest path to its completion criterion.
3. Name the first missing dependency.
4. Determine whether public/controlled evidence is sufficient.
5. If yes, implement the smallest reusable slice.
6. If no, create a bounded research/probe issue.
7. Keep unknown behavior explicit.
8. Re-cut generic work onto verified `main` rather than maintaining long
   speculative stacks.
9. Reject work that does not remove a dependency, strengthen a required
   invariant, or materially reduce future integration risk.

Competing-emulator features are **inspiration for likely dependency classes**,
not evidence that Astraea should implement them now.

## 14. Definition of ready

An implementation issue must state:

- gate/invariant advanced
- exact missing dependency
- evidence/spec references
- owned scope
- forbidden scope
- acceptance tests
- boundary/failure cases
- platform/CI expectations
- provenance/trace impact where relevant

## 15. Definition of done

A code issue is done only when:

- implementation is bounded;
- unit/integration tests pass;
- malformed/boundary cases are covered;
- sanitizer/fuzz requirements pass;
- warnings/formatting pass;
- exact PR head passes the required CI matrix;
- docs/ADR are updated if architecture changed;
- no undocumented title-specific workaround is introduced.

## 16. CI merge gate

Normal merge gate:

1. Linux x64
2. Windows x64
3. macOS ARM64
4. Linux ASan + UBSan
5. Linux Clang fuzz smoke

New untrusted parsers require actual fuzz execution, not just build coverage.

## 17. Current critical path

```text
graphics:
    V0 -> V1 -> V2 -> V3                         COMPLETE
                         |
                         +-> V4 differential      AS NEEDED
                         `-> V5 presentation      FUTURE

compatibility:
    C0 production diagnostic                     COMPLETE (Linux x86-64)
     |
     v
    C1 PS5 initial-process ABI / first instruction   ACTIVE (#300)
       C1A corroborated startup prefix            MERGED
       C1A observation validator                  MERGED (#308)
       C1B procparam relationship                 RESEARCH (#312)
     |
     v
    C2.0 first deterministic retail divergence
     |
     v
    C2 post-entry runtime/modules/HLE
     |
     v
    C3 deterministic boot
     |
     v
    C4 real-title headless GPU/frame
     |
     v
    C5 presentation/menu
     |
     v
    C6A in-game -> C6B playable route -> C6C reference-guarded

scalability/readiness:
    S0 fail-visible verification/provenance       ESTABLISHED
     |
     +-> S1 first-divergence/coverage             START WITH REAL TITLE
     +-> S2 cross-title regression corpus         GROW AFTER VISIBLE STATES
     +-> S3 architecture ratchets                 ADD WHEN DEFECT CLASSES APPEAR
     +-> S4 measured performance budgets          AFTER REPRESENTATIVE 3D
     `-> S5 release/user quality                  LATE
```

The exact merged next action belongs in `docs/STATUS.md`. Live open GitHub
issues/PRs provide in-flight state.
