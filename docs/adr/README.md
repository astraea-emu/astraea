# Architecture Decision Records

Architecture Decision Records (ADRs) capture decisions that would otherwise be rediscovered or silently changed.

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

- `0006-dependency-driven-vertical-integration.md` — choose the next smallest
  dependency on an end-to-end gate.
- `0007-separate-shader-semantic-and-compiler-ir.md` — preserve semantic
  Shader IR as the oracle and add compiler/value IR only when a workload needs
  it.
- `0008-verified-and-provisional-research-tracks.md` — keep hypotheses out of
  authoritative guest-visible behavior while allowing bounded downstream
  research.
- `0009-guest-gpu-image-and-surface-layout.md` — separate guest allocation,
  image interpretation, surface layout, and host materialization.
- `0010-supervised-retail-execution.md` — require a supervised syscall/isolation
  boundary before arbitrary retail execution.
- `0011-orthogonal-graphics-and-compatibility-gates.md` — track graphics
  integration and retail compatibility as orthogonal gates with a
  direct-title C0-C6 compatibility ladder.
