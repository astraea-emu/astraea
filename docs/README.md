# Astraea Documentation

This directory contains Astraea's durable technical record. Merged code is
authoritative for implemented behavior; these documents explain the current
frontier, architecture, evidence, and narrow behavioral contracts.

## Start here

- [`STATUS.md`](STATUS.md) — current merged frontier, blockers, and exact next action.
- [`PROJECT_PLAN.md`](PROJECT_PLAN.md) — architecture, V/C/S gates, milestone definitions, and dependency-selection policy.
- [`CLEAN_ROOM.md`](CLEAN_ROOM.md) — provenance and clean-room rules.
- [`DEVELOPMENT_MACOS.md`](DEVELOPMENT_MACOS.md) — macOS development setup.

## Architecture decisions

[`adr/`](adr/) records durable design decisions covering verification-first
architecture, guarded native execution, dependency-driven integration, shader
IR separation, supervised retail execution, compatibility gates, and
scalability/readiness.

See [`adr/README.md`](adr/README.md) for the index.

## Research and evidence

[`research/`](research/) contains the evidence record for PS5-specific behavior
and unresolved questions.

Current high-value entries:

- [`research/ps5_initial_process_abi.md`](research/ps5_initial_process_abi.md) — C1 process-entry evidence and promotion boundary;
- [`research/ps5_process_entry_probe.md`](research/ps5_process_entry_probe.md) — transport-neutral owned-hardware observation contract;
- [`research/scene_review_2026-10-07.md`](research/scene_review_2026-10-07.md) — current PS5-emulation scene comparison and architecture cross-check;
- [`research/sce_agc_link_shaders.md`](research/sce_agc_link_shaders.md) — LinkShaders evidence record;
- [`research/rdna2-ps5-graphics.md`](research/rdna2-ps5-graphics.md) — generic RDNA2 vs PS5-specific graphics evidence.

See [`research/README.md`](research/README.md) for the full research index.

## Specifications

[`specs/`](specs/) contains narrow versioned contracts for loader, memory,
execution, trace, and probe surfaces.

Specifications define interfaces and invariants; they are not compatibility claims.

## Source-of-truth order

When documentation and implementation disagree:

1. merged code/tests define implemented behavior;
2. [`STATUS.md`](STATUS.md) defines the intended current frontier;
3. accepted ADRs define durable architectural decisions;
4. research notes define evidence/confidence for PS5-specific behavior;
5. open GitHub issues and pull requests describe in-flight work.

Working-session notes and chat transcripts are intentionally not part of the
public repository.
