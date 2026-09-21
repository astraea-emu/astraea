# AstraeaProbe v0 — controlled behavioral probe format

**Status:** Proposed  
**Issue:** #10  
**Scope:** transport-neutral behavioral probe/request/result contract  
**Version:** `astraea.probe/*/v0`

## 1. Purpose

AstraeaProbe is the boundary between a behavioral question and one or more
controlled executions that attempt to answer it.

The format must work before Astraea has any hardware adapter. The same probe
case may later be run by:

- a pure host-side reference runner;
- Astraea's synthetic/native execution path;
- a lawful reference-hardware adapter.

Astraea core does **not** depend on the mechanism used to place or execute a
probe on reference hardware.

The format records behavior and provenance. It does not encode exploit,
jailbreak, authentication-bypass, firmware-decryption, or transport procedures.

## 2. Design goals

v0 prioritizes:

- stable probe identity;
- explicit typed inputs;
- comparable results across runners;
- deterministic normalization;
- provenance without embedding proprietary artifacts;
- forward-compatible JSON objects;
- separation between behavioral observations and diagnostic host noise;
- reproducible test vectors;
- a path into M3 trace/diff work without defining the trace schema prematurely.

## 3. Non-goals

AstraeaProbe v0 does not define:

- the PS5 application ABI;
- PS5 system-call or HLE identities;
- how a reference console is modified or reached;
- a firmware/module acquisition process;
- arbitrary executable upload;
- an RPC transport;
- M3's stable event trace schema;
- a benchmark format;
- wall-clock performance comparison.

## 4. Documents

v0 defines two JSON document kinds:

```text
astraea.probe.request/v0
astraea.probe.result/v0
```

A request defines one execution case and the environment in which it is
requested.

A result records one runner's outcome and observations.

Unknown object members must be ignored by readers unless a future schema marks
them required. Unknown enum values are not silently reinterpreted.

## 5. JSON encoding rules

All v0 documents are UTF-8 JSON.

v0 deliberately avoids JSON numeric ambiguity for architectural-width values.

### 5.1 Ordinary JSON integers

Schema/version counters that are guaranteed to remain below 2^53 may be JSON
integers.

Example:

```json
{ "version": 0 }
```

### 5.2 u64

A `u64` value is exactly:

```text
0x + 16 lowercase hexadecimal digits
```

Example:

```text
0x000000000000002a
```

No sign, whitespace, separators, or shortened representation is accepted in
the normalized form.

### 5.3 i64

An `i64` value is a base-10 string in minimal signed form.

Examples:

```text
0
-1
42
-9223372036854775808
```

A leading `+` or unnecessary leading zero is non-normalized.

### 5.4 bytes

Byte strings use lowercase hexadecimal in v0:

```json
{
  "type": "bytes",
  "encoding": "hex",
  "value": "48656c6c6f"
}
```

Normalized hex has even length and lowercase `a-f`.

The empty byte string is `""`.

### 5.5 strings

Strings are Unicode JSON strings. Probe authors should use UTF-8 semantic
strings only when text is genuinely part of the behavior being measured.
Opaque memory should be `bytes`.

## 6. Typed probe values

Probe `inputs` and result `observations` use explicit typed values.

v0 scalar forms are:

```json
{ "type": "u64",  "value": "0x000000000000002a" }
{ "type": "i64",  "value": "-1" }
{ "type": "bool", "value": true }
{ "type": "utf8", "value": "example" }
{ "type": "bytes", "encoding": "hex", "value": "00ff" }
```

A v0 probe should prefer a small flat map of named typed values.

Complex domain structures should receive a later versioned schema rather than
being hidden inside an untyped JSON object.

## 7. Probe identity

Every request contains:

```json
"probe": {
  "id": "astraea.reference.echo",
  "version": 0
}
```

### 7.1 id

`id` is a stable lowercase dotted identifier.

Recommended form:

```text
<owner-or-domain>.<area>.<name>
```

Examples:

```text
astraea.reference.echo
astraea.elf.entry-context
astraea.module.import-identity
```

Renaming semantic behavior requires a new probe id or version.

### 7.2 version

`version` is a non-negative integer.

Changing any behavior that could change a normalized result requires a version
increment.

Documentation-only wording changes do not.

## 8. Case identity

A **case** is the behavior being asked, independent of runner.

Its canonical projection contains exactly:

```json
{
  "probe": { ... },
  "inputs": { ... }
}
```

The normalized canonical JSON bytes are SHA-256 hashed.

The digest is written as:

```text
sha256:<64 lowercase hex digits>
```

This digest is called `case_sha256`.

The same probe/version/inputs therefore has the same case identity on Astraea,
a host reference runner, and reference hardware.

## 9. Request identity

The entire normalized request document is separately canonicalized and hashed.

That digest is `request_sha256`.

Unlike case identity, request identity includes:

- semantic target environment;
- requested capabilities;
- provenance/safety declarations.

A hardware request and an Astraea request for the same case are expected to
have different `request_sha256` values but the same `case_sha256`.

The request document does not contain its own digest.

## 10. Request schema

Top-level required members:

```json
{
  "schema": "astraea.probe.request/v0",
  "probe": { ... },
  "inputs": { ... },
  "environment": { ... },
  "provenance": { ... }
}
```

### 10.1 environment

Required v0 members:

```json
"environment": {
  "target_kind": "host_reference",
  "platform_family": "astraea",
  "architecture": "portable",
  "capabilities": [
    "reference_echo_v0"
  ]
}
```

`target_kind` values defined by v0:

- `host_reference`
- `astraea`
- `reference_hardware`

These are semantic execution classes, not transport names.

`platform_family` is a stable semantic label such as:

- `astraea`
- `ps5`
- `generic_x86_64`

`architecture` is a normalized architecture label such as:

- `portable`
- `x86_64`

`capabilities` is a set encoded as a lexicographically sorted JSON array with
no duplicates.

Optional semantic environment fields may include a lawfully known firmware or
platform version. A value that is unknown must be omitted rather than guessed.

### 10.2 provenance

Required v0 members:

```json
"provenance": {
  "source_kind": "astraea_owned_synthetic",
  "execution_scope": "host_only",
  "artifact_digests": []
}
```

Defined `source_kind` values:

- `astraea_owned_synthetic`
- `user_owned_controlled`
- `public_reproducible_fixture`

Defined `execution_scope` values:

- `host_only`
- `astraea_synthetic_guest`
- `controlled_reference_hardware`

`artifact_digests` is a set of content digests needed to identify an input
without embedding the artifact.

A digest entry may include:

```json
{
  "role": "probe_image",
  "sha256": "..."
}
```

The repository/result must not embed firmware, keys, proprietary SDK material,
copyrighted retail executables, or proprietary modules merely to make a probe
reproducible.

## 11. Result schema

Required top-level members:

```json
{
  "schema": "astraea.probe.result/v0",
  "probe": { ... },
  "case_sha256": "sha256:...",
  "request_sha256": "sha256:...",
  "outcome": "completed",
  "observations": { ... },
  "environment": { ... },
  "provenance": { ... },
  "diagnostics": { ... }
}
```

`probe` must exactly match the request.

`case_sha256` identifies the cross-run behavioral case.

`request_sha256` identifies the full normalized request consumed by this
runner.

## 12. Outcome

v0 defines:

- `completed` — the probe reached its defined normal terminal condition.
- `unsupported` — the runner cannot execute a required capability.
- `rejected` — the request violates runner policy or schema requirements.
- `fault` — the probe executed and reached a normalized fault outcome.
- `timeout` — the runner's explicit execution budget expired.
- `runner_error` — host/adapter infrastructure failed independently of probe
  behavior.

A runner must not translate `unsupported` into an apparently successful empty
observation set.

## 13. Observations

`observations` contains only behavior the probe definition says is
semantically observable.

Example:

```json
"observations": {
  "bytes_consumed": {
    "type": "u64",
    "value": "0x0000000000000010"
  },
  "exit_code": {
    "type": "u64",
    "value": "0x000000000000002a"
  },
  "output": {
    "type": "bytes",
    "encoding": "hex",
    "value": "48656c6c6f2066726f6d2070726f6265"
  }
}
```

Host addresses, host PIDs, thread IDs, absolute host paths, wall-clock
timestamps, and scheduler details are not behavioral observations.

## 14. Result environment

The result repeats the semantic environment actually used.

If a requested capability or environment field could not be honored, the
runner must not silently echo the request as though it were observed.

Runner-specific build identity may be recorded separately:

```json
"runner": {
  "name": "astraea",
  "version": "0",
  "commit": "<git sha>"
}
```

`runner` is provenance/diagnostic metadata. It is not part of cross-run
behavior equality unless a particular study explicitly chooses to group by it.

## 15. Diagnostics

`diagnostics` is for useful non-comparable data.

Examples:

- host error category;
- host OS code;
- runner log digest;
- optional duration;
- adapter version;
- an ephemeral run-instance identifier.

Diagnostics are excluded from the default behavior projection.

A result remains useful when all diagnostics are removed.

## 16. Behavior projection

The default cross-run behavior projection contains exactly:

```text
probe
case_sha256
outcome
observations
```

It intentionally excludes:

- request_sha256;
- target kind;
- runner build;
- timestamps;
- durations;
- host paths;
- host addresses;
- adapter metadata;
- transport metadata;
- diagnostic error strings.

This projection is canonicalized and may be hashed as `behavior_sha256` by
Lab tooling.

A result does not embed `behavior_sha256` in v0, avoiding self-referential
records and allowing later comparison policy to select a different projection.

## 17. Canonicalization

Canonicalization is deterministic and simple enough to implement independently.

For any normalized JSON value:

1. object keys are serialized in ascending Unicode code-point order;
2. arrays preserve their semantic order;
3. fields defined as sets must be sorted before serialization;
4. no insignificant whitespace is emitted;
5. strings use standard JSON escaping and UTF-8 output;
6. normalized v0 documents contain no floating-point numbers;
7. typed `u64`, `i64`, and `bytes` values use the lexical forms in this
   specification.

The reference fixtures in
`docs/specs/probe/examples/` include expected SHA-256 values so independent
implementations can test canonicalization.

## 18. Address normalization

A raw **host** pointer is never stable probe identity.

Guest addresses should be represented only when the probe explicitly makes
them observable.

Preferred future form when module identity exists:

```text
module identity + module-relative offset
```

For synthetic fixtures with deterministic fixed guest layouts, a guest virtual
address may be recorded as a typed `u64`, but the probe definition must say
that the address is semantically stable.

ASLR-affected absolute addresses must be normalized before entering
`observations`.

## 19. Sets versus sequences

JSON arrays are sequences by default.

If the specification calls a field a set, producers must:

- remove duplicates;
- sort lexicographically by the normalized element encoding.

Event order, byte order, argument order, and ordered observations must never be
sorted merely to make two runs compare equal.

## 20. Reproducibility

A probe result is reproducible when repeated runs with equivalent case inputs
and equivalent semantic environment constraints produce the same behavior
projection.

For evidence intended to become a platform rule, the recommended minimum is:

1. two consecutive identical normalized runs;
2. record the semantic environment;
3. retain request/result digests;
4. identify the probe definition/version;
5. document any runner or firmware variation tested.

Nondeterminism is evidence too. It must be represented explicitly rather than
discarded until a preferred result appears.

## 21. Timeouts and resource limits

A runner may impose:

- instruction/step budget;
- wall-clock safety timeout;
- output-size cap;
- guest-memory cap.

Budgets are runner policy unless the probe definition makes one part of the
case.

A timeout produces `outcome = "timeout"`; it is not `fault` and not
`unsupported`.

## 22. Fault normalization

A probe may define normalized fault observations without exposing host-specific
exception structures.

For example:

```json
"observations": {
  "fault_kind": {
    "type": "utf8",
    "value": "illegal_instruction"
  }
}
```

Detailed host signal/exception values belong in `diagnostics`.

Future M3 trace events may preserve richer fault records while still mapping to
the probe observation.

## 23. Relationship to M2 structured events

M2 currently exposes lightweight events such as:

- guest entry;
- gate stop;
- HLE resume;
- HLE exit.

AstraeaProbe v0 does not freeze those C++ types as a stable serialization
schema.

A probe may derive stable summary observations from them.

Full event streams belong to #6's trace schema and may later be referenced from
a probe result by digest.

## 24. Relationship to M3 trace artifacts

A future result may add an optional member such as:

```json
"artifacts": [
  {
    "kind": "astraea.trace/v0",
    "sha256": "..."
  }
]
```

The trace bytes are a separate artifact with their own schema.

Probe readers that do not understand the trace schema can still compare the
probe result.

## 25. Hardware adapter boundary

Conceptually:

```text
Probe request JSON
       |
       v
Runner adapter
       |
       +--> host reference
       +--> Astraea
       +--> optional reference-hardware runner
       |
       v
Probe result JSON
```

A hardware adapter is outside the clean-room core.

Core libraries must not import or require:

- exploit frameworks;
- jailbreak clients;
- firmware keys;
- proprietary system modules;
- authentication bypass code.

A user may provide lawful observations produced elsewhere. Astraea consumes
only the normalized request/result contract.

## 26. Safety/provenance requirements

Every probe definition must state:

- what behavior it asks;
- what code/artifact is executed;
- who owns or may lawfully use that artifact class;
- expected side effects;
- whether persistent external state is modified;
- maximum output/resource expectations.

Astraea-owned synthetic probes are preferred whenever they can answer the
question.

A probe must never require a copyrighted retail executable to be committed to
this repository.

## 27. Reference probe: astraea.reference.echo v0

The v0 reference probe exists only to test the probe pipeline.

It performs no guest execution and needs no platform-specific capability.

Inputs:

- `message: bytes`
- `exit_code: u64`

Semantics:

1. consume the input message;
2. observe exactly the same bytes as `output`;
3. report `bytes_consumed` equal to the message size;
4. report the provided `exit_code`;
5. complete.

Expected observations for the committed reference case:

```text
message        = "Hello from probe"
bytes_consumed = 16
exit_code      = 42
```

The host-side reference runner implemented with this specification validates
only the deterministic probe semantics: output bytes, consumed-byte count, and
exit code.

The committed request/result vectors separately define the future conformance
tests for:

- request parsing;
- typed values;
- canonicalization;
- case/request hashing;
- result serialization;
- behavior comparison.

Those JSON parser/serializer/hash components are **not** implemented by this
specification slice and must not be inferred from the reference runner test.

It intentionally defines **format plumbing**, not native execution.

## 28. Reference request test vector

See:

`docs/specs/probe/examples/reference-echo.request.json`

For that exact normalized request:

```text
case_sha256 =
sha256:963497f23ad64d000df07c6fee38b8ae2261be7861a9197b3cbb978c1f4609be

request_sha256 =
sha256:c3a6b360c47966a0d17bcac56f690930a875be1f696bea1e39d01020f652b119
```

The hashes are calculated from compact canonical JSON, not from the pretty
printed fixture bytes.

## 29. Reference result test vector

See:

`docs/specs/probe/examples/reference-echo.result.json`

The result uses:

- the same probe identity;
- the case/request hashes above;
- `outcome = completed`;
- exactly three deterministic observations.

A future serializer implementation must reproduce the same normalized content
regardless of host OS.

## 30. Validation rules

A v0 reader must reject at least:

- unknown top-level schema identifier;
- missing probe id/version;
- duplicate keys;
- malformed typed values;
- non-normalized u64/i64/hex encodings when normalized input is required;
- duplicate capability/set members;
- unsorted fields declared as sets when validating canonical input;
- malformed SHA-256 identifiers;
- a result whose probe identity disagrees with its request;
- a result with a request/case digest that does not match the consumed request;
- an impossible outcome/observation combination defined by that probe.

Unknown optional object members are preserved or ignored according to reader
policy, never silently converted into known semantics.

## 31. Versioning

`v0` is intentionally small.

A future v1 is required for incompatible changes such as:

- changing typed-value lexical forms;
- changing default behavior projection;
- changing canonicalization;
- redefining outcome meanings.

New optional object members can be added compatibly when v0 readers may ignore
them safely.

Individual probe semantics are versioned independently through
`probe.version`.

## 32. Implementation sequence after this spec

The next implementation work should be narrow:

1. implement v0 typed values and canonical JSON serialization;
2. implement request/case digest calculation;
3. implement the host-only `astraea.reference.echo` runner;
4. assert the committed request/result vectors;
5. define #6 trace schema independently;
6. map M2 execution events into #6 without changing Probe v0;
7. add a probe runner for `probe_hello` as a synthetic Astraea target;
8. only later add an optional reference-hardware adapter outside core.

## 33. Decision summary

> AstraeaProbe v0 is a transport-neutral JSON request/result contract. Probe
> case identity is derived from probe identity plus normalized inputs, while
> request identity also captures the semantic execution environment and
> provenance. Behavioral comparison uses probe identity, case identity,
> outcome, and normalized observations; host diagnostics and transport noise
> are excluded. Hardware execution is optional and external to the clean-room
> core. The committed host-only echo probe is the canonical v0 format test
> vector.
