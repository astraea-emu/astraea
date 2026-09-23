# LinkShaders tail reference-hardware observation

**Issue:** #193  
**Probe:** `astraea.ps5.agc.link-shaders-tail`  
**Probe version:** 0  
**Format:** AstraeaProbe v0  
**Scope:** validate and compare a controlled reference-hardware observation for
the four `sceAgcLinkShaders` records that remain unknown after #189.

## Why this probe exists

#188 established the bounded LinkShaders request contract and #189
transactionally reproduced every output byte currently supported by pinned
hardware measurement:

- 32 context interpolant records at `+0x000..+0x0ff`;
- one context routing record at `+0x108`:
  `{ offset = 0x29b, value = 2 }`.

The following native output remains unknown:

- context record `CX[32]` at `+0x100`;
- user-config records `UC[0]`, `UC[1]`, and `UC[2]`.

A public-source sweep found useful candidate identities and candidate shader
inputs, but not a committed native dump of these four returned records.

In particular:

- Orbistoun's later implementation/worklog explicitly treats only the
  interpolant table and `+0x108` qword as measured and preserves unspecified
  bytes rather than inventing them.
- `blackbearreloaded/ps5-opengl` contains native runtime diagnostics that
  print exactly `CX[32]` and `UC[0..2]` after a real LinkShaders call, but
  its public releases explicitly exclude raw console logs and no committed
  validation artifact containing those printed values was found.
- public compiler and shader-header projects expose linkage *inputs* such as
  stage-routing and GE state, but those inputs are not evidence that
  LinkShaders returns identical offset/value pairs.

Therefore #191 remains measurement-blocked. This probe makes the missing
measurement mechanically consumable without weakening that boundary.

## Clean-room boundary

The Astraea core implementation is a pure observation validator.

It does not define or include:

- a console transport;
- a jailbreak/exploit path;
- firmware or firmware keys;
- proprietary Sony SDK material;
- retail executables;
- proprietary shader binaries;
- an upload or RPC mechanism.

A lawful reference-hardware adapter remains outside Astraea core, exactly as
specified by `docs/specs/probe/astraea-probe-v0.md`.

The intended case uses independently generated/Astraea-owned synthetic shader
artifacts. Artifact identity should be carried by SHA-256 digests in the
AstraeaProbe provenance rather than embedding artifacts in the observation.

## Probe request semantics

Recommended stable identity:

```text
probe.id      = astraea.ps5.agc.link-shaders-tail
probe.version = 0
```

The case inputs should describe only behavior that defines the case. At
minimum:

- `primitive_type`: u64, value 4;
- `null_hull`: bool, true;
- `cx_sentinel`: u64 whose low byte is the repeated CX sentinel;
- `uc_sentinel`: u64 whose low byte is the repeated UC sentinel.

The provenance should identify the independently generated pre-raster and Pixel
probe artifacts by digest and role.

Recommended semantic environment:

```text
target_kind      = reference_hardware
platform_family  = ps5
execution_scope  = controlled_reference_hardware
```

Record a lawfully known firmware/platform version when available. Omit it when
unknown rather than guessing.

## Required result observations

The result should contain these behavior observations:

```text
link_return_code : i64
cx_raw           : bytes   # exactly 0x110 bytes
uc_raw           : bytes   # exactly 0x18 bytes
cx_sentinel      : u64
uc_sentinel      : u64
```

The complete raw blocks are the evidence source of truth.

Decoded register records may be added as diagnostic observations, but they must
not replace `cx_raw` or `uc_raw`.

## Validator contract

`validate_agc_link_shaders_tail_observation()` accepts:

- the signed LinkShaders return code;
- raw CX bytes;
- raw UC bytes;
- the two sentinel bytes.

It requires:

1. return code `0`;
2. exact CX size `0x110`;
3. exact UC size `0x18`;
4. `CX[0..31]` decode as:
   `{ offset = 0x191 + i, value = i }`;
5. `CX[33]` decodes as:
   `{ offset = 0x29b, value = 2 }`.

Only after those already-measured sanity checks pass does it expose the four
opaque observations:

```text
CX[32]
UC[0]
UC[1]
UC[2]
```

Each is returned only as an eight-byte little-endian offset/value record. The
validator assigns no register name or semantic interpretation to these records.

For each unknown record it also reports whether all eight bytes remained equal
to the supplied sentinel. An untouched record is a valid observation and must
not be rewritten into an assumed zero/default value.

## Repeat-run rule

`compare_agc_link_shaders_tail_runs()` compares the **complete raw CX block
first, then the complete raw UC block**.

Promotion into #191 requires at least two consecutive validated runs of the
same probe case with identical raw CX and UC outputs.

A mismatch reports:

- context vs user-config region;
- first differing byte offset;
- byte from run one;
- byte from run two.

The comparison deliberately covers known and unknown bytes. A run is not
considered reproducible merely because the four tail records happen to match.

## Native measurement procedure

A suitable external controlled runner needs only to:

1. initialize AGC;
2. create the independently generated type-2 pre-raster and Pixel shaders;
3. fill all `0x110` CX bytes with one non-zero repeated sentinel;
4. fill all `0x18` UC bytes with a different non-zero repeated sentinel;
5. call
   `sceAgcLinkShaders(cx, uc, 0, pre_raster, pixel, 4)`;
6. capture the signed return code and the two complete raw blocks;
7. restore both sentinel blocks and repeat the same case once more.

No draw or queue submission is required for this evidence question.

The already-measured sanity checks should reproduce before the tail result is
considered useful.

## What #193 does not establish

Passing this validator does not by itself make a new PS5 rule.

It establishes only that an observation is internally consistent with the
already-pinned LinkShaders behavior and exposes the exact opaque tail returned
by that run.

#191 may promote the four records only after:

- two identical validated runs;
- stable case identity/provenance;
- review of the captured raw bytes and environment;
- no contradiction with the existing evidence set.

Until then, #189's `measured_partial` LinkShaders output remains the complete
Astraea implementation boundary.
