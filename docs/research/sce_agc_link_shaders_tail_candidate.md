# sceAgcLinkShaders tail candidate model

This document defines a deliberately non-authoritative research model for the
four `sceAgcLinkShaders` output records that remain unmeasured for Astraea's
current V3 raster profile.

It exists so downstream architecture can be exercised without weakening the
evidence standard on `main`.

## What is measured

For the exact owned null-hull, type-2 Geometry + Pixel, primitive-4 profile,
Astraea currently treats these native LinkShaders outputs as measured:

- `CX[0..31] = {0x191 + i, i}`;
- `CX[33] = {0x29b, 2}`.

The following remain unknown native output:

- `CX[32]`;
- `UC[0]`;
- `UC[1]`;
- `UC[2]`.

Issue #191 remains the authoritative promotion gate.

## What public evidence supports

Public linked-output layouts plus independent PS5-oriented register maps strongly
corroborate these output identities:

| slot | candidate identity | register offset |
| --- | --- | ---: |
| `CX[32]` | `VGT_SHADER_STAGES_EN` | `0x2d5` |
| `UC[0]` | `GE_CNTL` | `0x25b` |
| `UC[1]` | `GE_USER_VGPR_EN` | `0x262` |
| `UC[2]` | `VGT_PRIMITIVE_TYPE` | `0x242` |

The values are not established by those identities.

The online audit also found multiple hardware-valid PS5 graphics profiles with
different stage/geometry-control values, including:

- `VGT_SHADER_STAGES_EN = 0x02002000`, `GE_CNTL = 0x00008040`;
- `VGT_SHADER_STAGES_EN = 0x02412010`, `GE_CNTL = 0x0000fc80`.

That difference is important evidence that these are pipeline-derived values,
not universal constants.

## Research API rule

`astraea::research::make_agc_link_shaders_tail_candidate()` therefore accepts
candidate pipeline values explicitly from its caller.

It does **not**:

- parse shader-header "specials";
- read guest memory;
- write guest memory;
- register an HLE function;
- make `kSceAgcLinkShadersHleId{5}` succeed;
- claim that native LinkShaders wrote its candidate records.

The helper only packages supplied values under the currently best-supported
candidate output identities.

Only primitive type 4 is accepted. This prevents the research helper from
silently generalizing beyond the exact production request profile already
bounded by the real LinkShaders planner.

## Why this branch exists

The research branch may use the candidate object to construct synthetic linked
state for downstream unit/integration experiments. Such experiments answer
questions like:

- can typed linked state be represented without losing register provenance?
- can a submitted Geometry + Pixel pair be bound transactionally?
- can draw-state planning remain independent of the eventual four native values?
- can the 4x4 offscreen raster path be decomposed so evidence replacement is
  localized?

The branch must not merge guest-visible success behavior into `main` until #191
receives the required repeated native observation.

## Human-intervention boundary

No user action is needed for this candidate model or for downstream synthetic
architecture work.

Human/reference-hardware intervention becomes necessary only when Astraea is
ready to promote the four unknown records into authoritative guest-visible
behavior. At that point the required action is the existing #191 protocol:

1. run the clean-room probe on reference PS5 hardware;
2. initialize CX and UC outputs with distinct non-zero sentinels;
3. call native `sceAgcLinkShaders` for the exact owned profile;
4. capture complete `0x110` CX and `0x18` UC raw outputs;
5. restore the sentinels and repeat the same invocation;
6. provide two byte-identical valid observations.

Until then, development on this branch must remain explicitly synthetic or
inferred.
