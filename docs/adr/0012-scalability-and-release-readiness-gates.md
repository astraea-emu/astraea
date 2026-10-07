# ADR 0012: Track scalability and release readiness as a third orthogonal gate axis

**Status:** Accepted  
**Date:** 2026-10-07

## Context

ADR 0011 separated graphics technology (V0-V5) from retail compatibility
(C0-C6). That separation remains correct, but the October 2026 PS5-emulation
scene demonstrates a third problem that neither axis measures:

> Does progress remain generic, measurable, regression-resistant and performant
> as the number and diversity of titles increases?

Current public projects show why this becomes load-bearing quickly:

- SharpEmu's public compatibility tracker reported 76 tested titles and 36
  reaching gameplay on 2026-10-07.
- prosper's compatibility tracker reported 61 tracked titles and 28 at
  gameplay-or-better on 2026-10-01, with reviewed route/snapshot guards for
  several titles.
- current SharpEmu work is heavily concentrated in cross-title HLE/service
  breadth, GPU-memory visibility, synchronization, hot-path allocation and
  performance work rather than just first-boot support.
- prosper uses data-driven shader coverage, reviewed snapshot guards,
  architecture ratchets, explicit performance invariants, and durable
  falsification records.
- PortPS5 separately measures library/export and RDNA decoder coverage.

These projects differ architecturally, and their implementations are not
Astraea's source of platform truth. Their project-scale failure modes are
nevertheless useful evidence about what a compatibility project must control
once real-title work accelerates.

Without a third axis, Astraea could advance C-gates while accumulating:

- title-id or executable-hash branches in shared code;
- silent success stubs with incorrect out-parameters;
- unmeasured GPU stalls/readbacks;
- per-draw/per-call allocation growth;
- compatibility fixes that regress previously working workloads;
- progress claims that are not tied to reproducible routes or baselines.

## Decision

Astraea keeps the V and C axes from ADR 0011 and adds a third orthogonal
**S0-S5 scalability/readiness axis**.

### S0 — fail-visible verification and provenance — established

The project has:

- typed unsupported/error boundaries;
- exact-head multi-platform CI;
- sanitizers and fuzz smoke;
- deterministic owned fixtures;
- AstraeaProbe / trace infrastructure;
- research/provenance and ADR discipline.

S0 remains a continuous invariant rather than a one-time finish line.

### S1 — measurable first-divergence and coverage accounting

Once real-title execution begins, Astraea measures the actual selected workload
rather than guessing platform-wide percentages.

At minimum, collect aggregate/local-only coverage for:

- modules/imports discovered;
- imports resolved;
- imports actually called;
- called HLEs with semantically implemented contracts;
- first unsupported CPU/syscall/HLE boundary;
- AGC/PM4 packet kinds observed;
- shader instructions decoded;
- shader instructions lowered;
- first unsupported shader operation;
- resource/synchronization classes encountered.

Retail bytes remain local. Public artifacts may contain only lawful metadata,
digests, normalized counts, names already available from public interface
sources, and typed first-divergence records.

### S2 — cross-title routes and regression guards

A visible or gameplay compatibility milestone is not durable until it has a
reproducible route and an appropriate regression check.

The local compatibility corpus should eventually span at least:

1. a small/custom 2D workload;
2. a Unity/IL2CPP workload;
3. an Unreal workload;
4. a demanding custom 3D workload.

Dumps, screenshots and other copyrighted title assets remain local and
gitignored. Reviewed summaries, hashes/thresholds and normalized results may be
recorded where lawful.

A compatibility fix that touches a shared subsystem is checked against the
available guarded corpus before merge when practical.

### S3 — architecture ratchets

Once real-title pressure begins creating compatibility debt, CI or repository
checks must make dangerous classes mechanically non-growing.

Candidate ratchets include:

- title IDs, executable hashes or shader hashes in shared core code;
- unregistered title-specific workarounds;
- unknown HLE calls returning success without evidenced semantics;
- host exceptions escaping through guest frames;
- behavior-changing untyped environment switches;
- blocking GPU waits/readbacks on steady-state paths;
- unbounded per-draw/per-HLE hot-path allocation growth;
- production SPIR-V emitters without validation coverage.

Ratchets are introduced only when the corresponding defect class exists and
the check can distinguish a real violation from legitimate code.

### S4 — measured performance budgets

Performance work begins from profiles, not intuition.

For representative 3D workloads, measure at least:

- CPU time by runtime/HLE/render phase;
- GPU submit/render/present time;
- blocking waits and readbacks;
- shader/pipeline compile events;
- hot-path allocation volume;
- frame progression/stall statistics;
- memory residency/cache pressure when relevant.

Correctness gates remain authoritative. A faster wrong result is not progress.

### S5 — release and user-quality readiness

Astraea becomes a user-facing emulator only after the underlying title path is
durable.

Release readiness eventually includes:

- reproducible packaged builds;
- crash/diagnostic bundles;
- stable per-title configuration surface without hidden behavior switches;
- controller/input, audio, save data and presentation quality;
- documented host/GPU/driver support;
- compatibility reports tied to exact builds;
- upgrade/migration behavior where user state exists.

S5 is intentionally late. Product polish must not become the C1-C4 critical
path.

## Compatibility-ladder refinement

ADR 0011's C0-C6 numbering remains stable.

C6 is refined into:

    C6A  in-game progression with recognizable scene output
    C6B  playable defined route with required input/audio/save behavior
    C6C  reference-validated and regression-guarded supported state

"Reaches gameplay" is therefore not equivalent to "playable", and "playable"
is not equivalent to an accuracy claim.

## First-title policy after C1

The first legally obtained retail workload is selected for **closure cost**, not
prestige.

Prefer a candidate with:

- small module/import surface;
- minimal network/entitlement dependence;
- deterministic startup route;
- modest shader/resource complexity;
- independently demonstrated feasibility in public implementations;
- usefulness as a representative engine/workload class.

A famous AAA title is a poor first target if a much smaller title can expose
the same runtime dependencies faster.

## Reuse policy

Other emulators and compatibility layers are comparative oracles for:

- likely dependency classes;
- experiment design;
- failure modes;
- test strategy;
- performance traps.

They are not PS5 hardware truth.

Direct code reuse requires both license compatibility and file-level provenance
review. A wholesale fork is not Astraea's default strategy.

## Consequences

### Positive

- Compatibility breadth can grow without silently eroding architecture.
- Progress is measured by first-divergence and guarded routes rather than
  screenshots alone.
- Performance optimization is delayed until it is measurable and relevant.
- The project can compare itself to mature competitors without cargo-culting
  their implementations.
- "Greatest emulator" becomes a multi-dimensional engineering objective:
  correctness, compatibility, performance, reliability and user quality.

### Negative / constraints

- Real-title progress requires more local evidence infrastructure than a simple
  boot/no-boot tracker.
- Some regression gates cannot run in public CI because lawful retail inputs
  remain local.
- Architecture ratchets add friction once enabled and therefore need precise
  definitions and escape hatches for reviewed exceptions.
- Compatibility percentages remain workload/corpus-dependent rather than a
  universal completion number.

## Alternatives considered

### Chase competitor compatibility counts directly

Rejected. It encourages broad speculative HLE/GPU implementation before Astraea
can measure its own first missing dependency.

### Make one title the permanent architecture target

Rejected. A single title cannot expose cross-engine, cross-runtime and
cross-resource failure modes and invites title-specific design.

### Add all expected S-gate infrastructure immediately

Rejected. ADR 0006 still governs: build the smallest infrastructure when its
failure mode becomes load-bearing.

## Evidence / references

- https://sharpemu.app/
- https://github.com/sharpemu/sharpemu
- https://github.com/mattias800/prosper
- https://github.com/mattias800/prosper/blob/main/COMPATIBILITY.md
- https://github.com/mattias800/prosper/blob/main/prosper/docs/process/VERIFICATION.md
- https://github.com/yuriolive/PortPS5
- ADR 0006
- ADR 0011
- issue #314

## Revisit triggers

Revisit this decision if:

- the C/V/S model creates duplicated or contradictory gates;
- a public-CI-safe compatibility corpus becomes possible without distributing
  proprietary title data;
- a different architecture produces materially better cross-title
  generalization under controlled comparison;
- release/product work becomes a primary project objective rather than a later
  readiness track.
