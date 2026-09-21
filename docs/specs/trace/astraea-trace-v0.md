# Astraea Trace v0 — stable behavioral trace contract

**Status:** Proposed  
**Issue:** #6  
**Schema:** `astraea.trace/v0`  
**Scope:** deterministic trace envelope, normalization, and serialization

## 1. Purpose

Astraea Trace v0 is the first stable machine-readable event contract for
loader/execution/HLE evidence.

It is deliberately smaller than Astraea's internal runtime state. The trace
contains only fields that a producer intentionally promotes into a durable
semantic record.

Trace v0 must support:

- deterministic synthetic Astraea runs now;
- differential comparison in #7;
- later controlled reference-hardware observations;
- future PS5-facing events without encoding guessed PS5 behavior today.

## 2. Non-goals

Trace v0 does not define:

- the PS5 ABI;
- PS5 system-call identities;
- a complete CPU instruction trace;
- a logging format for arbitrary host messages;
- a transport protocol;
- wall-clock performance benchmarking;
- a firmware acquisition or hardware-access mechanism.

A trace is evidence, not a debug-log dump.

## 3. Document form

A v0 trace is one UTF-8 JSON object:

```text
astraea.trace/v0
```

The normalized object contains:

```json
{
  "diagnostics": {},
  "events": [],
  "provenance": {},
  "run": {},
  "schema": "astraea.trace/v0"
}
```

The canonical serializer emits no insignificant whitespace and emits object
members in ascending Unicode code-point order.

## 4. Stable versus diagnostic data

v0 makes the classification explicit.

**Stable fields** are eligible for behavioral comparison. They must describe
guest/platform semantics rather than host accidents.

**Diagnostics** may contain useful runner-specific details but are excluded
from the default stable projection.

Examples of diagnostics:

- host signal/exception codes;
- host paths;
- host process/thread identifiers;
- wall-clock timestamps;
- elapsed host time;
- transient adapter messages.

A producer must not place a field in `stable` merely because it is easy to
capture.

## 5. Run metadata

Required run fields:

- `target_kind`
- `platform_family`
- `architecture`

They are stable lowercase semantic identifiers.

Optional probe linkage:

- `probe.id`
- `probe.version`
- `case_sha256`

`probe.id` and `probe.version` appear together or not at all.
`case_sha256` may appear only when probe identity is present.

Probe linkage uses AstraeaProbe identity but Trace v0 does not otherwise depend
on Probe v0 serialization.

## 6. Provenance

Required provenance fields:

- `source_kind`
- `producer.name`
- `producer.version`
- `artifact_digests`

Optional:

- `producer.commit`

`artifact_digests` is a set of `sha256:<64 lowercase hex>` identifiers.
Normalization sorts the set lexicographically and rejects duplicates.

Provenance identifies where evidence came from. It is not part of the default
event-stable projection used by #7.

## 7. Event identity

Every event has a `u64` event id serialized as:

```text
0x + 16 lowercase hexadecimal digits
```

Event order in the JSON array is semantic order.

Event ids must be **strictly increasing**. They need not be contiguous, which
allows a filtered trace to retain original identities without renumbering.

An event id is unique only within one trace.

## 8. Subsystem and type

Every event has:

- `subsystem`
- `type`

Both are stable semantic identifiers.

Examples:

```text
execution / guest_entry
execution / gate_stop
hle       / resume
hle       / exit
loader    / module_loaded
```

v0 does not reserve PS5-specific event names.

Unknown future subsystem/type pairs must not be silently reinterpreted as a
known event.

## 9. Identifier grammar

Semantic identifiers used by v0 are non-empty ASCII strings containing only:

```text
a-z 0-9 . _ : -
```

The first character must be `a-z` or `0-9`.

This applies to run identifiers, producer/source identifiers, field names, and
normalized guest object identifiers.

Human prose belongs in diagnostics or external documentation, not identity
fields.

## 10. Normalized guest location

A stable guest location is:

```json
{
  "kind": "object_offset",
  "object": "probe_hello.elf",
  "offset": "0x0000000000000010"
}
```

v0 supports only `kind = object_offset`.

The `object` is a stable semantic guest object identifier chosen by the event
producer. `offset` is relative to that object.

Raw host pointers are forbidden as stable guest identity.

ASLR-affected absolute guest addresses must not be promoted into `guest`
unless a future schema defines their normalization.

Future evidence may introduce stronger module identities without changing the
meaning of v0 `object_offset`.

## 11. Typed field values

Stable and diagnostic field maps use four v0 value forms.

### 11.1 u64

```json
{"type":"u64","value":"0x000000000000002a"}
```

### 11.2 bool

```json
{"type":"bool","value":true}
```

### 11.3 utf8

```json
{"type":"utf8","value":"astraea.test.write"}
```

The C++ serializer validates UTF-8 before emitting it.

### 11.4 bytes

```json
{"encoding":"hex","type":"bytes","value":"00ff"}
```

Hex is lowercase and has even length.

## 12. Field maps

The JSON `stable` and `diagnostics` objects are maps keyed by semantic
field name.

The C++ model accepts vectors for efficient construction. Normalization:

1. validates each name;
2. sorts entries lexicographically by name;
3. rejects duplicate names.

Event sequence order is never sorted.

## 13. Top-level diagnostics

The top-level `diagnostics` map is for run-wide non-comparable details.

It follows the same field normalization rules as event diagnostics.

Removing every diagnostic field must leave a useful behavioral trace.

## 14. Canonical serialization

The serializer:

1. normalizes the document;
2. emits UTF-8 JSON;
3. emits no insignificant whitespace;
4. writes object keys in ascending code-point order;
5. uses the exact u64/bytes lexical forms above;
6. preserves event array order;
7. emits normalized field/set order.

No floating-point values exist in v0.

The serializer is intentionally one-way in this slice. A general JSON reader is
future work and must reject duplicate JSON object keys before constructing v0
objects.

## 15. Default stable projection

The default stable comparison projection contains:

- `run.target_kind`
- `run.platform_family`
- `run.architecture`
- optional probe/case linkage
- event id
- subsystem
- type
- normalized guest location
- event `stable` fields

It excludes:

- provenance;
- top-level diagnostics;
- event diagnostics.

#7 may additionally choose comparison policies that ignore event ids after
alignment, but it must never compare diagnostics as platform behavior by
default.

## 16. Relationship to M2 events

Current M2 Linux synthetic sessions expose internal events:

- guest entry;
- gate stop;
- HLE resume;
- HLE exit.

Those C++ structs are **not** the v0 schema.

An adapter may map them into Trace v0 only after it supplies normalized guest
identity and stable semantic HLE names.

For example, an internal numeric `HleFunctionId` is not durable trace identity;
the canonical HLE name such as `astraea.test.write` is.

## 17. Relationship to Windows execution

Windows and Linux backend-specific signal/exception machinery must normalize to
the same stable event meaning when guest behavior is equivalent.

Windows exception numbers and POSIX signal values belong in diagnostics.

## 18. Forward compatibility

Future writers may add optional object members.

Readers must ignore unknown optional members they do not understand while
preserving known semantics.

A future incompatible change to:

- canonical lexical forms;
- event-id meaning;
- stable/diagnostic classification;
- guest-location semantics;
- required envelope fields

requires a new trace schema version.

## 19. Validation rules

The v0 C++ normalizer rejects at least:

- invalid semantic identifiers;
- invalid UTF-8 string field values;
- malformed SHA-256 identifiers;
- incomplete probe id/version pairing;
- `case_sha256` without probe identity;
- probe versions above JSON's exact-integer range;
- duplicate artifact digests;
- non-increasing event ids;
- duplicate stable field names;
- duplicate diagnostic field names.

## 20. Reference synthetic trace

The committed example models a four-event synthetic sequence:

1. execution guest entry;
2. execution host-gate stop for `astraea.test.write`;
3. HLE resume with result 16;
4. HLE exit with code 42.

The example is not a PS5 claim.

It exists to freeze:

- event ordering;
- normalized identifiers;
- typed field serialization;
- stable versus diagnostic separation;
- canonical output bytes.

## 21. Reproducibility

For a deterministic synthetic case, two runs should produce identical stable
projections.

Diagnostics may differ.

When a real platform behavior is nondeterministic, the producer must represent
that nondeterminism explicitly or retain multiple observations. It must not
delete divergent runs until a preferred result remains.

## 22. Safety and clean-room boundary

Trace artifacts must not embed:

- Sony firmware;
- keys;
- proprietary SDK material;
- decrypted retail executables;
- proprietary system modules;
- copyrighted game assets.

A trace may contain lawful content digests and normalized observations about
artifacts without embedding those artifacts.

Reference-hardware collection remains external to Astraea core.

## 23. Implementation boundary for this slice

This issue implements only:

- the v0 C++ data model;
- deterministic normalization;
- deterministic canonical serialization;
- one committed synthetic example;
- tests for ordering, duplicate rejection, identifier validation, UTF-8
  validation, and exact serialized bytes.

It does **not** yet:

- parse arbitrary JSON into Trace v0;
- adapt M2 runtime events automatically;
- compute trace digests;
- implement #7 diff/alignment;
- add PS5-specific event types.

## 24. Decision summary

> Astraea Trace v0 is a compact canonical JSON event document. Event ids are
> strictly increasing, guest locations are normalized as stable guest object
> plus relative offset, event data is explicitly split into stable fields and
> host/runner diagnostics, and provenance is retained without contaminating the
> default behavioral projection. The first implementation is serializer-only
> and uses a synthetic four-event trace to freeze normalization semantics.
