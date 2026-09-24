# Architecture Decision Records

Architecture Decision Records (ADRs) capture decisions that would otherwise be
rediscovered or silently changed.

## States

- Proposed
- Accepted
- Superseded
- Rejected

## Naming

`NNNN-short-title.md`, monotonically increasing.

## Required sections

- Status
- Date
- Context
- Decision
- Consequences
- Alternatives considered
- Evidence / references
- Revisit triggers

Use `0000-template.md` for new decisions.

## Current accepted architecture decisions

- `0001-verification-first-architecture.md` — correctness is driven by
  explicit evidence, tests, traces, and reproducible experiments rather than
  title-specific success.
- `0002-host-and-execution-strategy.md` — keep portable platform modeling
  separate from architecture-specific native execution.
- `0003-build-test-and-dependency-policy.md` — C++23/CMake, pinned
  dependencies, cross-platform CI, sanitizers, fuzzing, and warnings policy.
- `0004-clean-room-provenance.md` — provenance is a merge requirement and
  unauthorized proprietary material is excluded.
- `0005-guarded-native-x86-execution.md` — trusted owned probes may execute
  through explicit guarded native x86-64 backends.
- `0006-dependency-driven-vertical-integration.md` — choose the next smallest
  dependency on an end-to-end gate.
- `0007-separate-shader-semantic-and-compiler-ir.md` — preserve semantic
  Shader IR as the oracle and use a separate compiler/value layer as workloads
  require it.
- `0008-verified-and-provisional-research-tracks.md` — keep hypotheses out of
  authoritative guest-visible behavior while allowing bounded downstream
  research.
- `0009-guest-gpu-image-and-surface-layout.md` — separate guest allocation,
  image interpretation, surface layout, and host materialization.
- `0010-supervised-retail-execution.md` — require a supervised
  syscall/isolation boundary before retail native execution and compatibility
  diagnostics.
