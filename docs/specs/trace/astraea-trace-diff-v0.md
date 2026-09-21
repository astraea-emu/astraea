# Astraea Trace Diff v0 — deterministic first-divergence contract

**Status:** Proposed  
**Issue:** #7  
**Depends on:** Astraea Trace v0 (#6)  
**Scope:** stable-projection comparison and first-divergence reporting

## 1. Purpose

Trace Diff v0 compares two Astraea Trace v0 documents and identifies the
earliest meaningful behavioral divergence.

The comparator is intentionally defined over the Trace v0 **stable
projection**, not canonical JSON bytes and not runtime diagnostics.

## 2. Input normalization

Both traces are normalized with the Trace v0 normalizer before comparison.

A malformed left or right trace is a typed comparison error. It is not reported
as a behavioral divergence.

Normalization therefore freezes:

- identifier validation;
- UTF-8 validation;
- artifact-digest set ordering;
- stable/diagnostic field ordering;
- duplicate-field rejection;
- strictly increasing event ids.

## 3. Default stable projection

Diff v0 compares exactly:

- `run.target_kind`
- `run.platform_family`
- `run.architecture`
- optional probe id/version
- optional case SHA-256
- event order
- event ids
- event subsystem/type
- normalized guest object + relative offset
- event stable fields and typed values

It ignores by default:

- provenance;
- top-level diagnostics;
- event diagnostics.

This matches the projection frozen by Trace v0.

## 4. Event semantic identity

For alignment, an event identity is:

```text
(subsystem, type, normalized guest location)
```

Event id and stable-field values are deliberately excluded from the alignment
identity.

This allows a caller to distinguish:

- the same semantic event with a changed value;
- an event inserted before a later known event;
- an event deleted before a later known event;
- an event whose semantic identity itself changed.

## 5. First-divergence order

Comparison proceeds in this order:

1. stable run metadata;
2. event sequence from the start;
3. event id, unless policy disables id comparison;
4. stable fields in normalized lexical field-name order.

The first mismatch terminates comparison and becomes the machine-readable
result.

## 6. Insertion/deletion alignment

When current event identities differ, Diff v0 searches forward independently:

- in the right trace for the current left identity;
- in the left trace for the current right identity.

If only one side has the nearer unambiguous match:

- a nearer match on the right means one or more events were inserted on the
  right;
- a nearer match on the left means one or more events were deleted from the
  right.

If both candidate distances are equal, or neither identity reappears, the
result is an `event_identity_mismatch` rather than guessing a reorder.

This is intentionally a deterministic first-divergence heuristic rather than a
global minimum-edit alignment algorithm.

## 7. Event ids

Trace v0 includes event ids in the default stable projection, so Diff v0
compares them by default.

A policy may set:

```text
compare_event_ids = false
```

This is useful when two already-aligned traces retain different producer-local
event numbering.

Disabling event-id comparison does not affect event ordering or semantic
identity.

## 8. Ignored stable fields

Diagnostics are already excluded. In addition, a caller may explicitly ignore
selected event stable fields.

An ignore rule contains:

- optional subsystem;
- optional event type;
- required field name.

Missing subsystem/type acts as a wildcard. Field names and supplied semantic
identifiers use the same identifier grammar as Trace v0.

Example intent:

```text
ignore field "counter" only on execution/sample events
```

Rules are exact, normalized, and duplicate rules are rejected.

Run identity fields cannot be ignored by v0 policy. If a run target/platform,
architecture, probe identity, or case identity differs, the comparison must say
so explicitly.

## 9. Machine-readable result

A successful comparison returns:

- `equivalent`;
- number of fully matched events before divergence;
- optional first-divergence record.

A divergence record contains enough typed data for future regression
automation:

- divergence kind;
- stable path;
- left/right event index when applicable;
- left/right event id when applicable;
- left/right semantic event identity when applicable;
- field name when applicable;
- left/right typed stable value when applicable;
- insertion/deletion event count when applicable.

No host pointer or diagnostic field is promoted into the result.

## 10. Divergence kinds

v0 defines:

- `run_field_mismatch`
- `event_id_mismatch`
- `event_insertion`
- `event_deletion`
- `event_identity_mismatch`
- `stable_field_missing_left`
- `stable_field_missing_right`
- `stable_field_value_mismatch`

A later schema may add richer alignment/minimization metadata without changing
the meaning of these v0 kinds.

## 11. Human-readable report

The formatter consumes the machine-readable result; it does not recompare the
traces.

Reports identify the first stable path or semantic event and include typed
left/right values when available.

The human report is diagnostic presentation. Automation must consume the typed
result rather than parse report text.

## 12. Determinism

For the same normalized traces and policy, Diff v0 must return the same result
on Linux, Windows, and macOS.

No wall-clock time, host address, host exception/signal number, thread id, or
container iteration order may affect comparison.

## 13. Complexity boundary

Diff v0 stops at the first divergence.

At a semantic identity mismatch it performs two forward linear searches for a
possible re-alignment point. It does not construct a global edit-distance
matrix.

This keeps memory usage bounded while still identifying common
insertion/deletion regressions.

## 14. Out of scope

Diff v0 does not:

- parse arbitrary JSON;
- automatically adapt M2 events into Trace v0;
- minimize a failing trace;
- assign probabilistic similarity;
- reorder events to force a match;
- compare provenance as behavior;
- infer that a nondeterministic stable field should be ignored.

Automatic minimization can build on this typed first-divergence result later.

## 15. Clean-room boundary

The diff layer operates on normalized trace observations only.

It does not require or embed Sony firmware, keys, proprietary SDK material,
decrypted retail executables, proprietary modules, game assets, or
DRM/circumvention material.

## 16. Decision summary

> Astraea Trace Diff v0 normalizes both Trace v0 documents, compares stable run
> identity exactly, aligns events by subsystem/type/normalized guest identity,
> compares event ids by default, supports explicit scoped stable-field ignore
> rules, and returns the first typed behavioral divergence. Diagnostics and
> provenance are excluded from behavioral comparison by construction.
