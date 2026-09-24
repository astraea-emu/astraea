# ADR 0008: Separate verified and provisional research integration tracks

**Status:** Accepted  
**Date:** 2026-09-24

## Context

Astraea is verification-first and currently has a real evidence blocker in
`sceAgcLinkShaders`: four native output records for the selected owned
Geometry + Pixel profile remain unmeasured.

At the same time, several downstream dependencies are independently grounded
in public AMD behavior or already-verified Astraea interfaces. Examples include
generic PM4 register transport, draw control, guest GPU allocation identity,
surface-layout arithmetic, compiler plumbing, and Vulkan backend mechanics.

Treating every evidence blocker as a total repository stop would make hardware
access a prerequisite for unrelated architecture work. Treating a plausible
hypothesis as production truth would be worse: it would weaken Astraea's
verification contract and could allow one guessed Sony-specific value to
silently contaminate later work.

The project therefore needs an explicit policy for advancing independent work
past a platform-specific evidence blocker without turning an experimental
branch into a second, unofficial emulator.

## Decision

Astraea uses two integration tracks.

### Verified track

The verified track is the only path to authoritative guest-visible behavior on
`main`.

A change may enter this track when every load-bearing platform behavior it
implements is supported by:

- public primary documentation;
- sufficiently strong public independent evidence;
- controlled authorized observation; or
- an already accepted Astraea contract derived from those evidence classes.

Unknown Sony-specific behavior remains unsupported. A verified path must not
return guest-visible success by substituting a candidate value for an
unmeasured native result.

### Provisional research track

A bounded provisional branch/PR may continue beyond an evidence blocker when
its purpose is to answer an architectural question or implement an
independently evidenced dependency.

Provisional work must:

- label every hypothesis and evidence gap explicitly;
- identify the verified commit/PR it is stacked on;
- avoid wiring unverified behavior into guest-visible successful HLE/runtime
  paths;
- preserve enough provenance to replace hypotheses cleanly when better
  evidence arrives;
- keep synthetic fixtures owned and redistributable;
- retain the normal test/sanitizer/fuzz expectations for the code it touches;
- be disposable or re-cuttable rather than becoming a permanent parallel
  product branch.

A provisional branch may use a typed hypothesis as an input to exercise
downstream architecture, but the hypothesis must remain mechanically
distinguishable from measured/verified state.

### Promotion and re-cutting

A provisional result is promoted only after its load-bearing assumptions are
verified.

Independently evidenced generic work discovered while stacked on a
provisional branch should be re-cut onto the verified frontier when practical.
This keeps `main` advancing without implying that the original blocker has
been solved.

Long research stacks are temporary. If a stack grows enough that unrelated
generic work is trapped behind one hypothesis, split/re-cut the generic work
rather than maintaining a shadow implementation indefinitely.

### Hardware as a targeted evidence oracle

Controlled hardware observation is allowed at any vertical gate when it is the
smallest way to resolve a specific evidence blocker.

This is distinct from V4 whole-workload differential testing:

- **targeted evidence probe:** answers one bounded semantic question and may
  occur during V1-V3;
- **V4 differential:** compares a broader owned workload across reference
  hardware and Astraea at stable state/trace/output boundaries.

Astraea core remains independent of console transport and circumvention
tooling.

## Consequences

### Positive

- Hardware availability no longer blocks unrelated generic architecture work.
- The project can explore downstream interfaces without laundering hypotheses
  into production behavior.
- Generic AMD/backend work can reach `main` independently of a Sony-specific
  blocker.
- Evidence provenance remains explicit.
- Research branches remain replaceable rather than becoming a second source of
  truth.

### Negative / constraints

- Some work may need to be re-cut from a provisional stack before merge.
- Documentation must clearly distinguish merged, provisional, and hypothetical
  behavior.
- CI success on a provisional branch proves implementation consistency, not
  correctness of the hypothesis that motivated it.

## Alternatives considered

### Stop all downstream work at the first evidence blocker

Rejected because it makes unrelated progress depend on hardware access and
contradicts dependency-driven vertical integration.

### Merge plausible candidate behavior with a warning

Rejected because guest-visible success would make the warning non-binding and
future work would naturally depend on the candidate as if it were fact.

### Maintain a permanent experimental branch

Rejected because it would create a shadow emulator whose assumptions drift
away from the verified source of truth.

## Evidence / references

- ADR 0001: verification-first architecture.
- ADR 0004: clean-room provenance.
- ADR 0006: dependency-driven vertical integration.
- Issue #191: current LinkShaders evidence blocker.
- #193/#194: transport-neutral LinkShaders tail observation validation.

## Revisit triggers

Revisit if:

- provisional work repeatedly cannot be re-cut cleanly;
- research branches routinely exceed a manageable review horizon;
- hardware observation becomes continuously available and removes the need for
  provisional downstream exploration; or
- the distinction fails to prevent unverified behavior from reaching
  guest-visible runtime paths.
