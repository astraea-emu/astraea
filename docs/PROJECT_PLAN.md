# Astraea Project Plan

## 1. Objective

Build a clean-room, verification-first PlayStation 5 compatibility research and emulation project whose correctness is driven by documented evidence, controlled experiments, deterministic tests, and regression localization rather than title-specific hacks.

Astraea is a long-horizon systems project. Speed comes from parallelizing well-specified work, automating validation, and refusing to encode unsupported assumptions.

## 2. Source of truth

GitHub is authoritative.

- `docs/PROJECT_PLAN.md` — strategy and milestone dependency graph.
- `docs/STATUS.md` — current state, active work, blockers, and next tasks.
- `docs/CHAT_HANDOFF.md` — procedure for moving to a fresh AI conversation.
- `docs/adr/` — architecture decisions and their evidence.
- GitHub issues — executable work units.
- Pull requests — review and integration history.
- CI — mechanical acceptance gate.

Chats are working sessions, not project memory.

## 3. Core engineering principles

1. **Evidence before behavior.** Platform-specific behavior must cite public documentation, controlled observations, or an explicitly documented hypothesis.
2. **Clean-room provenance.** Never commit Sony firmware, keys, proprietary SDK files, decrypted game content, proprietary source code, or material whose redistribution is not authorized.
3. **Portable core, specialized execution.** Loader, memory models, parsers, traces, HLE contracts, and IRs should be host-portable. Native x86-64 guest execution is enabled only on compatible hosts.
4. **Verification-first.** Every subsystem should expose structured state that can be tested, traced, diffed, fuzzed, and minimized.
5. **Small PRs.** A change should solve one bounded problem and include tests. Large speculative rewrites require an ADR first.
6. **No compatibility hacks without explanation.** Title-specific workarounds require a documented root cause or an explicit temporary quarantine.
7. **Reproducibility.** Toolchain versions, dependency revisions, experiments, fixtures, and expected outputs are recorded.
8. **Security posture.** Treat guest binaries and captured inputs as untrusted. Parsers and loaders are fuzz targets from the beginning.

## 4. Host strategy

### Development host

macOS is a first-class development environment for repository work, portable components, analysis, documentation, tests, and tooling.

### Runtime truth

The PS5 CPU ISA is x86-64. Astraea's eventual native-execution fast path therefore targets x86-64 hosts first:

- Linux x86-64
- Windows x86-64

Apple Silicon macOS is ARM64. It must not be treated as equivalent to an x86-64 execution host. Portable components should run locally; native guest-execution validation runs in x86-64 CI or dedicated x86-64 hardware.

### Graphics

Vulkan is the first host graphics API. macOS testing may use MoltenVK as a portability layer, but MoltenVK is not the semantic oracle for native Vulkan behavior. Native GPU validation will eventually require Linux/Windows hardware.

## 5. Workstreams

### A. Foundation
Repository policy, build system, CI, sanitizers, static analysis, formatting, test framework, dependency policy, contribution rules, provenance rules.

### B. Loader and modules
ELF64 validation, program headers, mappings, relocations, dynamic metadata, import/export representation, module graph, stack/TLS preparation.

### C. Guest memory and execution
Virtual address abstraction, permissions, fault model, guest context, native x86-64 transitions, exception handling, controlled entry/exit, later instrumented execution.

### D. HLE/platform model
Function registry, ABI contracts, threads, synchronization, events, timers, filesystem, process/module services, audio, input, video services.

### E. Astraea Lab
Structured trace schema, normalization, deterministic portions, differential comparison, snapshots, automatic bisect integration, failure minimization, corpus management.

### F. Astraea Probe
Small controlled programs/experiments, result schema, hardware-side capture where legally and technically appropriate, reproducibility metadata.

### G. GPU
Command/state frontend, register/state model, Astraea Graphics IR, RDNA2 shader decoder, Shader IR, SPIR-V emission, Vulkan backend.

### H. User experience
Game/library management, configuration, compatibility database integration, debugger/trace viewer. Deferred until the core is useful.

## 6. Milestones and gates

### M0 — Engineering foundation

Deliver:
- repository layout
- C++23/CMake policy
- reproducible developer setup
- Linux x64, Windows x64, macOS ARM64 CI
- formatting/lint policy
- unit-test framework
- ASan/UBSan jobs where supported
- fuzzing entry point
- ADR template
- clean-room/provenance policy
- issue templates
- status/handoff documents

Exit gate: a trivial library + test builds cleanly on all required CI hosts.

### M1 — Validated guest image

Deliver:
- strict ELF64 parser
- overflow/bounds validation
- PT_LOAD model
- guest mapping plan
- relocation representation
- import/export metadata representation
- stack/TLS construction primitives
- malformed-input fuzzing

Exit gate: synthetic owned ELF fixtures load deterministically and malformed variants fail safely.

### M2 — Controlled execution

Deliver:
- guest CPU context
- x86-64 host execution path
- guarded guest/host transition
- HLE dispatch registry
- guest exit path
- exception/fault translation
- synthetic probe executable

Exit gate:

```
astraea probe_hello.elf
ELF loaded
guest x86-64 entered
guest -> HLE write
guest -> HLE exit(42)
trace emitted
PASS
```

### M3 — Verification infrastructure

Deliver:
- stable trace schema
- normalization rules
- event IDs
- trace diff
- snapshot/corpus format
- regression bisect tooling
- automatic divergence localization
- minimal reproduction framework for supported traces

Exit gate: a deliberately introduced behavioral regression is automatically detected and localized.

### M4 — Platform/HLE expansion

Order:
1. memory services
2. process/module basics
3. threads
4. synchronization
5. time/timers
6. events/queues
7. filesystem
8. input
9. audio/video support interfaces

Each family requires a behavioral spec and tests before broad implementation.

### M5 — Graphics foundation

Deliver:
- command/state capture model
- GPU frontend interfaces
- Astraea Graphics IR
- initial RDNA2 instruction decoder
- Shader IR
- SPIR-V backend
- Vulkan device/backend abstraction

Exit gate: controlled synthetic graphics workloads produce validated host output without title-specific paths.

### M6 — Integration/compatibility

Only here do commercial-title compatibility experiments become a primary workstream.

Compatibility categories must distinguish:
- load
- boot
- menu
- in-game
- playable
- accurate

A title is not evidence that the implementation is correct.

## 7. AI/agent operating model

### Phase 1
One lead reasoning thread + one implementation agent.

- ChatGPT reasoning thread: architecture, research synthesis, task specifications, review.
- Codex: bounded implementation, local builds/tests, PR preparation.
- GitHub: source of truth.

### Phase 2
Add a second agent only when there are independent tasks or an adversarial review need.

Preferred second-agent use:
- falsify assumptions
- find UB/races
- generate adversarial tests
- independently review ABI/memory/GPU changes

Do not have two agents rewrite the same subsystem concurrently unless running a deliberate comparison experiment.

## 8. Definition of ready for an implementation issue

An issue is ready only if it includes:

- problem statement
- evidence/spec references
- owned files/directories
- explicitly forbidden scope
- interface constraints
- acceptance tests
- failure cases
- platform matrix
- expected artifacts
- dependencies/blockers

## 9. Definition of done

A code issue is done only when:

- implementation is bounded to scope
- unit/integration tests pass
- relevant malformed/boundary cases exist
- sanitizer jobs pass where applicable
- formatting/static checks pass
- trace/behavior expectations are updated if applicable
- documentation/ADR is updated if semantics changed
- PR review resolves all blocking findings
- no undocumented compatibility workaround is introduced

## 10. Parallelization rule

Parallelize across stable interfaces, not across ambiguity.

Good:
- ELF parser
- trace schema tooling
- CI hardening
- RDNA2 documentation research

Bad:
- three agents each inventing the HLE ABI
- GPU backend work before command/state contracts exist
- title hacks while memory semantics are unknown

## 11. Near-term critical path

```
M0 foundation
    |
    +--> loader contracts --------+
    |                             |
    +--> memory contracts --------+--> M1 guest image
    |                             |
    +--> trace schema ------------+
                                  |
                                  v
                           M2 controlled execution
                                  |
                   +--------------+--------------+
                   |                             |
                   v                             v
             M3 verification                 HLE research
                   |                             |
                   +--------------+--------------+
                                  v
                             M4 platform
                                  |
                                  v
                             M5 graphics
                                  |
                                  v
                         M6 compatibility
```

## 12. Efficiency rules

- No manual boilerplate that can be generated deterministically.
- Prefer scripts and CMake presets over setup instructions that drift.
- Cache dependency/build artifacts in CI when safe.
- Keep expensive GPU jobs out of ordinary PR CI until needed.
- Use synthetic fixtures before real game inputs.
- Record failed approaches so agents do not repeat them.
- Update `docs/STATUS.md` after every meaningful merge.
- Keep one active critical-path issue per agent unless tasks are truly independent.
