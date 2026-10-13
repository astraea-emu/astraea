# ADR 0013: Measure CPU execution backends against guest-state traces

**Status:** Proposed  
**Date:** 2026-10-13

## Context

Astraea's existing non-native C1 interpreter executes exactly thirteen original
source-backed Breakout instructions under a synthetic entry context, stopping
*before* its first unresolved imported CALL at guest RIP `0x23c`. It preserves
the real `RELATIVE` relocation and all nineteen original unbound
`GLOB_DAT` slots. Source-owned Linux native ptrace proofs establish selected
syscall and memory-write controls, but [#410](https://github.com/astraea-emu/astraea/issues/410)
now independently demonstrates writable non-guest tracee VMAs. Host IP-scoped
syscall traps are **not** a hostile-code sandbox, nor are instruction-by-
instruction ptrace exceptions a practical general x86-64 game CPU.

We need a measured backend decision without either (a) attempting to implement
all x86-64 semantics in piecemeal native tracer guards, or (b) importing a
large emulator framework without establishing compatibility, legal provenance,
guest-memory ownership, and performance.

## Proposed decision

1. **One guest semantic contract, multiple potential host backends.** A CPU
   engine may advance a guest instruction only through Astraea-owned virtual
   mappings and CPU context. It reports each *committed* instruction with
   before/after guest RIP, full GPR/RFLAGS/FS/GS state, and the current
   single-write subset's exact guest-VA address, **explicit width (0 or 8
   bytes)** and contents. Multi-write, vector and variable-width effects
   must be added to this contract and independently qualified before those
   opcodes can be evaluated. For the current supported subset, unsupported
   calls and denied writes must refuse **before** changing guest CPU/memory
   state and must emit no retirement event. **This is not a universal x86
   fault-atomicity rule:** a repeating string instruction can commit earlier
   iterations before an exception, leaving RIP at the faulting instruction
   and source/destination/count registers at a restartable intermediate state.
   A future backend contract must represent these already-committed effects,
   the precise fault stage, and partial CPU state without falsely retiring the
   whole instruction. A memory-access hook notification alone does not prove
   successful memory commitment. Instruction budgets are strict and original
   mapped bytes have their own immutable provenance.
2. **Current source-backed interpreter is the initial oracle, not the final
   CPU engine.** It now optionally emits caller-owned, bounded exact
   per-retirement records on both original Breakout and independently
   Astraea-authored tests; it is neither feature-complete nor trusted as an
   independent semantic oracle for unsupported opcodes. Compare future
   candidates against authoritative x86 behavior on source-owned fixtures.
3. **Evaluate one mediated execution candidate in an isolated research
   environment before selecting an external engine.** No new runtime
   dependency enters release builds from this ADR alone. Compare the candidate
   on exact source-owned machine code, then the same pinned original Breakout
   until the *same typed first import stop*, with identical initial guest
   state and mapping/relocation bytes. Never fabricate a Sony provider,
   modify Breakout to ease the experiment, or resume past an unknown service.
4. **Native acceleration is conditional.** ADR 0010 remains authoritative:
   untrusted native guest entry is blocked until host-memory/control-flow
   containment (including all implicit writes, memory aliases, descendants,
   signals and host syscall escapes) has passed adversarial verification.
   Source-owned native controls and a syscall filter do not establish it.
5. **macOS ARM64 is a first-class analysis/test host**, but native PS5
   x86-64 instructions do not execute directly on Apple Silicon; translation
   or an appropriate compatibility environment is required.

## Backend evaluation candidates — research, not selections

| Candidate | Relevant evidence | Integration / suitability caveat |
| --- | --- | --- |
| Existing Astraea Zydis-interpreted subset | Direct control over virtual W+X guest model, typed stops, source-backed Breakout; existing cross-platform CI | Tiny ISA subset, limited performance and conformance; do not confuse a thirteen-step stop with gameplay |
| [FEX](https://github.com/FEX-Emu/FEX) | MIT-licensed optimized x86-64 translation on **AArch64 Linux** | Linux user-mode/x86 rootfs assumptions; neither a drop-in PS5 CPU library nor native macOS Apple Silicon support shown by project docs |
| [Blink](https://github.com/jart/blink) | ISC-licensed x86-64 Linux emulator; project documents macOS ARM and Linux operation, JIT | Linux ABI/ELF lifecycle already integrated; assess extractability of guest CPU and syscall separation, accuracy and perf before adoption |
| [QEMU/TCG](https://www.qemu.org/docs/master/about/emulation.html) | Mature cross-host translation and execution/memory instrumentation; QEMU TCG plugins can observe instruction/memory operations | A plugin cannot itself rewrite emulator system state; TCG embedding/API and code/license boundaries need review |
| [Unicorn](https://github.com/unicorn-engine/unicorn) | CPU emulator with C APIs and memory/fault hooks, multiple host architectures | Its upstream license is labelled GPL-2.0; assess version-specific GPLv3 compatibility, redistribution/linking and callback guarantees **before linking or vendoring** |

These are primary-project capability reports, **not** performance benchmarks or
proofs of PS5 ABI compatibility. No third-party emulator implementation should
be copied or linked into Astraea without a separately approved license/provenance
review. No candidate is declared a winner in this **Proposed** ADR.

## Measurable acceptance protocol for a future candidate

- Freeze source SHA, guest image/profile, synthetic startup context, virtual
  map/read/write/execute permissions and all relocation bytes; attest the
  original independent Breakout ELF SHA
  `05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8`.
- Compare every committed guest CPU context (all register fields), exact RIP
  sequence, guest-memory stores and final memory readback. Compare the
  **typed first unsupported stop** separately; a failed instruction has no
  retirement or trace event. Require negative mutated bytes, unmapped/RX
  writes, invalid entry state, budget exhaustion and source-authentication
  failures to remain visible.
- Differentially test opcode families against an independent x86 semantic
  oracle; identical output from two Astraea helpers is not enough for
  semantic correctness. Raw RFLAGS remains diagnostic data: comparison across
  engines must mask **architecturally undefined bits per opcode** (e.g. AF
  after AND), while demanding agreement on all *defined* flag bits. No
  undefined-bit mask may be invented without independent ISA evidence.
- Record measured first-pass/warm throughput, repeated p50/p95 latency,
  memory use and test-host identity on supported hosts. Numeric performance
  claims require actually running benchmarks; CI runtimes are not a
  substitute for controlled backend measurements.
- Keep all seven exact-head CI jobs as the default integration ratchet,
  including both pinned C1 workflows. A candidate failing containment or
  semantic parity is **rejected**, not made compatible by a guessed HLE return.

## Consequences

### Positive

- A stable, concrete CPU state/memory-effect comparison boundary drives
  integration rather than a large architectural rewrite or test count.
- The portable path can model guest W+X virtually without exposing RWX
  native host pages.
- Linux/Windows and Apple Silicon strategy can diverge in implementation
  while preserving a common guest-visible contract.

### Negative / constraints

- Complete mediated x86-64 semantics and accurate PS5 service ABI may require
  substantial upstream adaptation and independent observations.
- Capturing full CPU state per step costs memory/time and is research-only;
  production execution must use an appropriate bounded/sampled tracing mode.
- CPU backend progress alone cannot unlock the first Sony imported service,
  genuine PS5 startup ABI or a title-produced frame.

## Alternatives considered

### Continue extending exact native ptrace opcode guards until retail works

Not selected for general untrusted admission. Numerous implicit/stack/vector
writes, host mappings and IPC/lifetime effects make this route neither a
scalable instruction engine nor proven sandbox.

### Directly import a translation engine now

Deferred. None of the candidates has passed Astraea's source-owned guest
memory/CPU trace/stop parity, license, embedding, host security and portability
acceptance together.

### Simulate the first imported service with a fixed return value

Rejected; Breakout's discarded source-level return does not authenticate
Sony API side effects or initialization contracts.

## Evidence / references

- [#378](https://github.com/astraea-emu/astraea/issues/378):
  source-pinned independent Breakout and first typed imported-service stop.
- [#410](https://github.com/astraea-emu/astraea/issues/410):
  native containment threat model, non-guest writable tracee VMAs, and
  source-owned pre-retirement memory permission proofs.
- Merged #450–#458: checked relocation/virtual guest memory, static
  post-call dependency audit and restricted source-owned native tests.
- `tools/reference/ps5_process_entry_observer/owned_cpu_step.*`:
  existing bounded non-native decoder/interpreter and optional comparison trace.
- Upstream primary documentation: FEX, Blink, QEMU/TCG, Unicorn at the
  project URLs above; licenses require version-specific verification.

## Revisit triggers

A measured, independently verified guest instruction/memory-effect mismatch;
a proven enforceable native isolation design; an accepted third-party CPU
engine meeting the common trace/stop contract; a new legally permissible
backend license; or a workload proving the selected backend is incompatible
with required instruction families.
