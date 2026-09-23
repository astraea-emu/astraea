# Astraea Project Plan

## 1. Objective

Build a clean-room, verification-first PlayStation 5 compatibility research
and emulation project whose correctness is driven by public specifications,
controlled experiments, deterministic tests, and regression localization
rather than title-specific hacks.

Astraea is a long-horizon systems project. Efficiency means reaching real
end-to-end emulator behavior with the fewest unsupported assumptions—not
maximizing raw commit count, opcode count, or HLE surface area.

ADR 0006 defines the current planning model: **dependency-driven vertical
integration**.

## 2. Source of truth

GitHub is authoritative.

- `docs/PROJECT_PLAN.md` — durable strategy, architectural workstreams, and
  vertical integration gate definitions.
- `docs/STATUS.md` — the canonical **merged frontier**: completed state,
  blockers, and exact next dependency/action expected on `main`.
- Open GitHub issues / pull requests — authoritative in-flight work and branch
  state. Unmerged PR behavior is not completed project behavior.
- `docs/CHAT_HANDOFF.md` — procedure for moving to a fresh AI conversation.
- `docs/adr/` — durable architecture decisions and their evidence.
- `docs/research/` — evidence notes and bounded format/behavior research.
- GitHub issues — executable work units.
- Pull requests — review and integration history.
- CI — mechanical acceptance gate.

Chats are working sessions, not project memory.

## 3. Core engineering principles

1. **Evidence before behavior.** PS5-specific behavior must cite public
   documentation, controlled lawful observations, or an explicitly labeled
   hypothesis.
2. **Clean-room provenance.** Never commit Sony firmware, keys, proprietary SDK
   files, decrypted retail content, proprietary system modules, or material
   whose redistribution is not authorized.
3. **Guest semantics before host mapping.** Model SCE/AGC/RDNA2 behavior in the
   guest domain before translating it to Vulkan or another host API.
4. **Portable core, specialized execution.** Parsers, state models, HLE
   contracts, traces, and IRs remain host-portable. Native x86-64 guest
   execution is enabled only where the host architecture permits it.
5. **Verification-first.** Every subsystem should expose structured state that
   can be tested, traced, diffed, fuzzed, and minimized.
6. **Small, vertical PRs.** A change should remove one real dependency on the
   next integration path and include tests. Large speculative rewrites require
   an ADR first.
7. **No compatibility hacks without explanation.** Title-specific workarounds
   require a documented root cause or an explicit temporary quarantine.
8. **Reproducibility.** Toolchain versions, evidence, fixtures, experiments,
   and expected outputs are recorded.
9. **Security posture.** Treat guest binaries and captured inputs as untrusted.
   Binary parsers and loaders are fuzz targets from the beginning.
10. **Unknown means unknown.** Undocumented Sony-specific behavior is a named
    evidence blocker, not an invitation to invent a plausible constant.

## 4. Host strategy

### Development hosts

macOS, Linux, and Windows are first-class development environments for
portable repository work.

### CPU runtime truth

PS5 software executes x86-64 CPU code. Astraea's native guest-execution path
therefore targets:

- Linux x86-64
- Windows x86-64

Apple Silicon macOS is ARM64. It remains useful for portable analysis,
parsers, IRs, compiler work, and tests, but it is not equivalent to an x86-64
native execution host.

### Graphics host strategy

Vulkan is the first host graphics API.

The guest model is not Vulkan-shaped. PS5 AGC state, guest resources,
synchronization, and RDNA2 shader semantics remain represented independently
of the backend. MoltenVK may provide portability coverage on macOS, but native
Vulkan behavior on Linux/Windows remains the primary backend target.

SPIR-V is the first host shader IR because Vulkan consumes SPIR-V shader
modules. Astraea's existing Shader IR remains the guest-semantic/oracle layer
and stays independent of SPIR-V so guest semantics can be tested without a
Vulkan device. When a concrete workload first requires SSA/value dataflow,
structured control flow, resource operations, or stage I/O, Astraea introduces
the smallest separate compiler/value IR required by that workload. That layer
also remains free of Vulkan handles and preserves provenance back to semantic
Shader IR/RDNA2. See ADR 0007.

## 5. Stable architectural workstreams

These workstreams are **not** a waterfall. They advance when required by the
next vertical gate.

### A. Foundation and quality

Repository policy, C++23/CMake, CI, sanitizers, fuzzing, dependency policy,
review conventions, and reproducibility.

### B. Loader and modules

ELF/SCE validation, mappings, relocations, dynamic metadata, import/export
identity, module graph, stack/TLS preparation, and later module lifecycle.

### C. Guest memory and CPU execution

Guest address-space model, permissions/faults, x86-64 guest context,
guest/host transitions, native execution, controlled exits, and later
exception/instrumentation needs.

### D. Platform HLE

Exact import identity, ABI contracts, memory/process/module services, threads,
synchronization, events, time, filesystem, input, audio, video, and other
system interfaces.

HLE is expanded **on demand by an owned probe or integration gate**, not by
trying to clone the entire platform API up front.

### E. Astraea Lab / Trace

Stable traces, normalization, deterministic serialization, first-divergence
diffing, snapshots, minimization, regression corpora, and controlled hardware
comparison.

### F. Astraea Probe

Owned synthetic programs and reproducible experiments that isolate one
platform behavior at a time. Hardware-side observation is used when legally
and technically appropriate.

### G. PS5 GPU frontend

AGC shader containers/objects, command buffers, register/state decoding,
submission semantics, resource descriptors, guest GPU memory, surfaces,
synchronization, and presentation state.

### H. RDNA2 shader semantics

AMD-documented instruction decoding, semantic Shader IR, CFG, state semantics,
exact oracle execution where useful, floating-point modes,
memory/image/export semantics, and other instruction families pulled by real
workloads.

"Generic RDNA2" means AMD-defined guest ISA semantics shared by the hardware
family. It is required emulator behavior, not placeholder data.

### I. Shader compiler and host GPU backend

Semantic Shader IR -> workload-driven compiler/value IR -> SPIR-V lowering and
validation, followed by Vulkan device/resource/pipeline management,
synchronization mapping, headless execution, then presentation. The current
straight-line vector-probe emitter remains a valid narrow proof and does not
need a speculative rewrite before a workload requires the compiler IR.

### J. User experience

Configuration, game/library management, compatibility UI, debugger/trace
viewer, and other product surfaces. Deferred until core execution paths are
useful.

## 6. Completed foundation milestones

These milestones established reusable infrastructure. They remain useful
historical gates but no longer dictate a strict subsystem sequence.

### M0 — Engineering foundation

Completed: repository/build/test/CI/provenance/ADR foundations.

### M1 — Validated guest image

Completed: strict owned-fixture ELF parsing, bounds validation, mapping and
relocation foundations, and fuzzing.

### M2 — Controlled execution

Completed for trusted Astraea-owned synthetic probes on supported x86-64
hosts: guest image -> native guest entry -> HLE -> controlled exit.

### M3 — Verification infrastructure

Core Trace v0, normalization, deterministic serialization, first-divergence
comparison, AstraeaProbe v0, and evidence maps are established. Verification
infrastructure continues to evolve alongside later gates.

## 7. Active vertical integration gates

These gates express the shortest meaningful end-to-end proofs. A later gate
does not imply every earlier subsystem is globally complete.

### V0 — PS5 shader ingestion — complete

```text
AGC shader container
    -> validated PS5-specific envelope
    -> bounded RDNA2 words
    -> existing RDNA2 decoder
    -> semantic Shader IR
```

Completed by #134/#135 with owned synthetic fixtures, opaque provenance
preservation, malformed-input tests, and dedicated fuzz smoke.

### V1 — Guest-created shader object — complete

```text
owned SCE guest probe
    -> exact import/HLE boundary
    -> sceAgcCreateShader
    -> validated/prepared guest-domain shader
    -> persistent created-shader identity
```

Completed through #140/#141, #145/#147, #148/#149, #162/#163, and
#164/#165. The real owned SCE guest now reaches `sceAgcCreateShader`, prepares
the evidence-scoped pixel object, materializes/retains its canonical AGC +
RDNA2 + semantic Shader IR record, and publishes guest state transactionally
without Vulkan dependency.

Program GPU addresses are treated as typed guest GPU-domain identity and are
not assumed globally unique.

### V2 — Validated host shader module — complete

```text
supported semantic Shader IR
    -> deterministic SPIR-V 1.6 module
    -> Vulkan-environment validation
```

Completed through #150/#151 for the bounded straight-line vector probe
(NOP/V_MOV_B32/V_ADD_F32/END). The generated module is validated with
SPIRV-Tools for the Vulkan 1.3 environment and remains deterministic.

This bounded gate does not mean every semantic Shader IR operation is
compilable. New compiler coverage remains workload-driven, and the existing
wave interpreter remains the semantic oracle rather than the production
rendering engine.

### V3 — Headless GPU execution — active

Full gate:

```text
controlled guest-domain GPU workload
    -> AGC command/state frontend
    -> guest resources
    -> semantic Shader IR / compiler lowering / SPIR-V
    -> Vulkan
    -> deterministic host-visible result
```

The first host-GPU semantic/backend proof is complete through #152/#153:
Linux CI forces Mesa Lavapipe, executes the V2 module headlessly on Vulkan,
and requires bit-for-bit state-buffer equality with Astraea's semantic
interpreter.

That proof is intentionally narrower than the full V3 gate. The remaining V3
path is pulled in slices:

1. **Submission-side shader binding** — captured DCB -> Type-3 framing ->
   SET_SH_REG IR -> persistent shader state -> pixel program GPU address ->
   unique created-shader lookup.
2. **First guest resource-backed workload** — add only the command,
   descriptor/resource, memory/export, synchronization, and backend behavior
   demanded by one owned deterministic workload.
3. Continue expanding by first missing dependency, never by raw opcode/API
   coverage.

Guest GPU virtual addresses are guest-domain identifiers. They must resolve
through Astraea's guest GPU memory/resource model and must not be cast to CPU
guest pointers, host pointers, Vulkan handles, or `VkDeviceAddress` values.

Submission provenance must remain traceable from raw DCB bytes/word offsets
through packet/state effects, selected guest shader identity, compiler
lowering, emitted SPIR-V, and backend-visible evidence. Unknown packet/state
semantics remain explicit blockers rather than silently ignored behavior.

### V4 — Controlled PS5 differential

Run the same Astraea-owned workload, when authorized hardware access is
available, through a controlled PS5 observation path and Astraea. Compare
stable traces/state/output rather than relying on visual intuition.

Hardware access is a **validation accelerator**. It is not a prerequisite for
V0-V3 work that is already supported by public evidence.

If a V1-V3 behavior cannot be established from public evidence, record a
specific hardware-evidence blocker rather than guessing.

### V5 — Presentation

Goal:

```text
guest draw / flip state
    -> guest synchronization
    -> VideoOut model
    -> host presentation
```

No title-specific display path. Headless correctness remains testable
independently of the window/presentation layer.

## 8. Compatibility phase

Commercial-title experiments become increasingly useful only after the
relevant execution paths exist.

Compatibility categories must distinguish at least:

- load
- boot
- menu
- in-game
- playable
- accurate

A title reaching one category is integration evidence, not proof that the
underlying implementation is semantically correct.

Arbitrary retail guest execution remains disabled until the project
explicitly defines the required safety, provenance, and execution gates.

## 9. Dependency-driven issue selection

After every meaningful merge:

1. Identify the next incomplete vertical gate.
2. Trace the shortest path from current state to that gate.
3. Name the first missing dependency on that path.
4. Ask whether its behavior is already supported by public evidence.
5. If yes, create the smallest implementation issue that removes it.
6. If no, create a bounded research/probe issue and record the evidence blocker.
7. Reject work that does not remove a dependency, strengthen a required
   invariant, or materially reduce future integration risk.

### Examples

Good next-task reasons:

- "V1 cannot create a shader object because the guest-memory ABI boundary is
  not represented."
- "V2 cannot validate SPIR-V because Shader IR has no stage I/O contract for
  the selected probe."
- "V3 cannot bind the probe's buffer because the guest descriptor semantics are
  unknown."

Bad next-task reasons:

- "This opcode is easy to add."
- "This API is probably common in games."
- "A competing emulator has this feature."
- "This refactor might be useful later."
- "We can make a game boot by hard-coding this value."

## 10. Parallelization rule

Parallelize across stable interfaces, not across ambiguity.

Good:

- a platform-HLE dependency for an owned probe;
- SPIR-V backend scaffolding once the Shader IR contract is stable;
- independent AGC evidence research;
- parser fuzzing;
- trace tooling.

Bad:

- two agents inventing different meanings for the same undocumented AGC field;
- Vulkan resource code before the guest resource contract is known;
- broad opcode expansion with no target workload;
- title hacks while foundational semantics are unresolved.

When two tasks touch the same uncertain interface, resolve the evidence and
contract first.

## 11. Definition of ready for implementation

An implementation issue is ready only when it states:

- the vertical gate or invariant it advances;
- the exact missing dependency;
- evidence/spec references;
- owned files/directories where practical;
- interface constraints;
- explicitly forbidden scope;
- acceptance tests;
- failure/boundary cases;
- platform/CI expectations;
- dependencies or evidence blockers;
- expected trace/provenance behavior when relevant.

## 12. Definition of done

A code issue is done only when:

- implementation is bounded to scope;
- unit/integration tests pass;
- malformed/boundary cases exist where applicable;
- sanitizer jobs pass where applicable;
- fuzz coverage is added for new untrusted parsers;
- formatting/warnings checks pass;
- trace/behavior expectations are updated if semantics changed;
- documentation/ADR is updated if an architectural contract changed;
- exact PR head passes the required CI matrix;
- no undocumented compatibility workaround is introduced.

## 13. CI merge gate

The normal public merge gate remains:

1. Linux x64
2. Windows x64
3. macOS ARM64
4. Linux ASan + UBSan
5. Linux Clang fuzz smoke

Parser work must ensure the relevant fuzz target is actually executed by the
fuzz-smoke job, not merely compiled. The newer PM4 Type-3 stream framer and
bounded RDNA2 stream decoder/lowerer should receive dedicated fuzz-smoke
coverage as a hardening follow-up; that work is independent of the active V3
critical path.

## 14. Efficiency rules

- Prefer one end-to-end dependency removed over many disconnected features.
- Reuse stable IR/state boundaries instead of bypassing them.
- Preserve raw provenance so new evidence does not require re-capturing inputs.
- Use owned synthetic fixtures before broad software inputs.
- Keep unsupported behavior typed and explicit.
- Do not generalize a field until more than the current gate requires it.
- Prefer deterministic generators/scripts over manual fixtures.
- Record failed hypotheses so they are not rediscovered.
- Keep expensive GPU/runtime jobs out of ordinary CI until their gate needs
  them; once needed, make them reproducible.
- Update `docs/STATUS.md` after every meaningful merge.
- Keep one active critical-path issue per agent unless independent work can
  merge without competing assumptions.

## 15. Current critical path

The durable dependency order is:

```text
completed foundation
    |
    v
V0  AGC container -> RDNA2 -> semantic Shader IR            COMPLETE
    |
    v
V1  owned guest -> persistent AGC shader identity            COMPLETE
    |
    v
V2  supported semantic Shader IR -> validated SPIR-V         COMPLETE
    |
    v
V3  submitted guest state/resources -> Vulkan -> result      ACTIVE
    |   host-GPU semantic proof complete (#153)
    |   next slice: submission-side created-shader binding
    v
V4  controlled PS5 differential when evidence requires it
    |
    v
V5  guest flip/VideoOut -> host presentation
```

Platform HLE, RDNA2 coverage, GPU commands/resources, compiler lowering, and
verification tooling feed this path only when the next owned workload requires
them. They are not separate finish-all phases.

The merged next dependency/action is recorded in `docs/STATUS.md`.
Any in-flight branch/issue is discovered from live open GitHub PRs/issues
rather than hard-coded into durable roadmap text.
