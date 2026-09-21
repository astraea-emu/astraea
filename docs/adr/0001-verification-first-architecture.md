# ADR 0001: Verification-first architecture

**Status:** Accepted  
**Date:** 2026-09-20

## Context

PS5 emulation contains large areas where incomplete public knowledge can tempt title-specific patching or undocumented assumptions. Astraea needs a development model that scales from synthetic tests to hardware-backed behavioral evidence without making commercial-title booting the primary correctness metric.

## Decision

Astraea will be verification-first.

Core subsystems must expose structured, testable state. Platform-specific behavior should be represented by specifications/tests before broad implementation. Astraea Lab will provide trace capture, normalization, comparison, regression localization, and behavioral corpora.

Commercial-title compatibility is an integration outcome, not the early architectural driver.

## Consequences

### Positive
- Incorrect assumptions can be isolated early.
- Regressions can be localized systematically.
- Hardware observations can be incorporated as tests instead of folklore.
- AI-generated implementation can be judged against explicit contracts.

### Negative / constraints
- Early visible compatibility may progress more slowly than hack-driven development.
- Trace schemas and test infrastructure require up-front work.
- Some nondeterministic behavior will require normalization rather than exact replay.

## Alternatives considered

### Compatibility-first implementation
Rejected as the primary methodology because booting a title does not demonstrate semantic correctness and tends to accumulate fragile special cases.

### Fork an existing emulator
Rejected as the default architecture because it would inherit another project's technical and licensing constraints and weaken Astraea's independent verification model.

## Evidence / references

See `docs/PROJECT_PLAN.md` and `docs/CLEAN_ROOM.md`.

## Revisit triggers

Revisit if verification infrastructure creates sustained critical-path cost without preventing measurable regressions, or if future evidence shows a different architecture substantially improves correctness and maintainability.
