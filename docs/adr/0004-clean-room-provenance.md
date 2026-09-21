# ADR 0004: Clean-room provenance is a merge requirement

**Status:** Accepted  
**Date:** 2026-09-20

## Context

Emulator projects routinely interact with public reverse-engineering research, other open-source implementations, vendor specifications, and controlled hardware observations. Without provenance discipline, it becomes difficult to distinguish independent behavioral knowledge from copied implementation detail or unauthorized material.

## Decision

Astraea treats provenance as part of correctness.

Any non-trivial PS5-specific behavior added to the project must be supportable by documented public evidence, controlled observation, or an explicitly marked hypothesis. Unauthorized proprietary material is excluded from the repository. Third-party code incorporation requires explicit license review.

## Consequences

### Positive
- Cleaner legal/technical boundaries.
- Future contributors can audit why behavior exists.
- Conflicting evidence can be resolved scientifically.

### Negative / constraints
- Some implementation work must wait for evidence.
- Documentation overhead is intentional.

## Alternatives considered

### Informal contributor judgment only
Rejected because provenance becomes impossible to reconstruct over a multi-year project.

## Evidence / references

See `docs/CLEAN_ROOM.md`.

## Revisit triggers

The policy may become stricter as the contributor base grows; its core provenance requirement should not be weakened.
