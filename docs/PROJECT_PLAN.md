# Astraea Project Plan

## 1. Objective

Build a clean-room, verification-first PlayStation 5 compatibility research
and emulation project whose correctness is driven by public specifications,
controlled experiments, deterministic tests, and regression localization
rather than title-specific hacks.

Efficiency means removing the **first real dependency** on the shortest
end-to-end path, not maximizing opcode count, HLE surface area, or visible
compatibility claims.

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
- Windows x86-64: owned native/supervisor proofs, but arbitrary retail
  admission remains disabled until equivalent pre-kernel syscall containment
  exists.
- Apple Silicon macOS: portable analysis/compiler/test host, not native PS5 CPU
  execution.

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

C1 is the current critical path (#300).

Promote it in layers:

#### C1A — entry registers and parameter block

Establish the smallest corroborated contract for entry register values,
parameter-block shape, argc/argv interpretation, teardown callback, and stack
state.

#### C1B — process metadata

Establish required process/procparam metadata and its relationship to the
loader-provided entry state.

#### C1C — primary-thread TLS/TCB

Establish TLS allocation, TCB structure requirements, and initial FS/GS bases.

#### C1D — bootstrap ordering

Establish which dynamic/module/runtime initialization must be complete before
entry for the selected workload.

**C1 completion criterion:** one selected legally obtained or independently
owned title/profile may execute its first native retail instructions without
using Astraea's synthetic owned-probe stack and without inventing unknown entry
state.

### C2 — runtime/bootstrap closure

Advance from first instruction through the first real runtime dependencies:

- module graph / runtime linker
- relocation/import completion
- HLE-vs-LLE module policy
- primary thread/TLS
- first guest syscall/HLE services
- process/thread/handle model
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

### C6 — in-game / playable / accuracy progression

Compatibility reporting must distinguish at least:

- diagnostic
- native-entry
- boot
- first-frame/headless-GPU
- menu
- in-game
- playable
- accurate

A category is an integration observation, not proof of semantic correctness.

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

## 10. Workload-driven expansion after C1

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

Do not turn the structural ELF parser into a runtime linker.

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

- guest memory page tracking / invalidation
- buffer and image caches
- alias tracking
- pipeline/shader cache
- descriptor/resource discovery
- queue/scheduler/synchronization model

These are expected eventual needs, not current speculative tasks.

### Shader compiler maturity

When real shaders exceed the current bounded compiler profile:

- SSA/value flow
- dominance/phi handling
- structured control flow
- resource discovery/lowering
- stage I/O lowering
- optimization passes
- deterministic compiler cache keys

Preserve the semantic Shader IR as the correctness/provenance layer.

## 11. Independent evidence tracks

### LinkShaders (#191 / #207)

Complete guest-visible `sceAgcLinkShaders` only when the unknown returned tail
records are measured for the exact supported profile. Public compiler inputs
or plausible register values are not returned-output evidence.

This track advances when it becomes the first dependency of a selected
workload or authorized hardware capture is available.

### Linux defense in depth (#297 / #298)

pidfd and Landlock are worthwhile post-C0 hardening, not blockers for #300.

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
     |
     v
    C2 bootstrap/modules/TLS/HLE
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
    C6 in-game/playable/accuracy
```

The exact merged next action belongs in `docs/STATUS.md`. Live open GitHub
issues/PRs provide in-flight state.
