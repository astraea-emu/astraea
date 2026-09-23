# PS5 AGC LinkShaders request evidence

**Accessed:** 2026-09-23  
**Scope:** evidence-bounded request validation and created-shader identity
resolution for the owned no-tessellation triangle-list raster path.

This note deliberately separates the public `sceAgcLinkShaders` call/output
contract from output bytes that have actually been measured. Astraea does not
fill unmeasured native state merely because a public caller allocates space for
it.

## Public import identity

Pinned public stub data:

- repository: `claimore22/ps5rs`
- commit: `6577dae1359adc15de727a907d9d83ff894e4977`
- file: `data/stubs/by_library/libSceAgc.txt`

records:

```text
MqAdbRMdNz4 sceAgcLinkShaders
```

For Astraea's owned SCE fixture where libSceAgc is the first and only imported
library/module, the existing long-form encoding rules produce:

```text
MqAdbRMdNz4#A#B
```

As with CreateShader, `#A#B` is fixture-local encoded library/module identity,
not a universal suffix for libSceAgc.

## Six-argument call shape

Pinned SharpProspero:

- repository: `SvenGDK/SharpProspero`
- commit: `9220876e25bc28aca1f65ea644783a479949ad77`
- files:
  - `src/SharpProspero/Interop/Agc/SceAgc.cs`
  - `src/SharpProspero/Graphics/Renderer3D.cs`

declares the call as:

```text
sceAgcLinkShaders(
    cxShaderLinkage,
    ucPrimitiveState,
    hullShader,
    geometryShader,
    pixelShader,
    primitiveType)
```

and uses a null hull handle when tessellation is absent.

Pinned ps5link SDK and pinned native homebrew independently use the same
no-tessellation shape with a pre-raster/geometry shader handle, pixel shader
handle, and primitive value `4` for triangle-list rendering:

- `Rufidj/ps5link-sdk@ea771e535378740b6a058b8e5419eb8a0e0e0ec8`
- `mpereiraesaa/ps5-agc-gears@1ae1f9182abd2770c131b97419034fb85173c2dc`

## Output allocation shape

Independent public callers reserve:

- 34 context records = `0x110` bytes;
- 3 user-config records = `0x18` bytes.

Pinned `ps5-agc-gears` names those record slots as:

Context:

1. 32 `SPI_PS_INPUT_CNTL` records;
2. `VGT_SHADER_STAGES_EN`;
3. `VGT_GS_OUT_PRIM_TYPE`.

User-config:

1. `GE_CNTL`;
2. `GE_USER_VGPR_EN`;
3. `VGT_PRIMITIVE_TYPE`.

Its public register record is eight bytes. Astraea treats this as observable
ABI/allocation-shape evidence, not proof of every value LinkShaders writes.

## Measured output subset

Pinned public measurement work in
`project-oops/Orbistoun@3212e2af5d226d9139e3f59c80735fa8d6271d0e`
records hardware-observed LinkShaders output from an obSCEne measurement
campaign.

The measured default interpolant table is 32 eight-byte records:

```text
record i:
    offset = 0x191 + i
    value  = i
```

The first two observed qwords were therefore:

```text
0x0000000000000191
0x0000000100000192
```

The same measurement records one routing qword at context byte offset
`+0x108`:

```text
0x000000020000029b
```

interpreted under the public eight-byte offset/value record shape as:

```text
offset = 0x29b
value  = 2
```

The measurement explicitly leaves the rest of the routing block unmodelled
rather than inventing values.

## Why #188 is planner-only

The current evidence does **not** pin all values for:

- the context record at `+0x100`;
- the three user-config output records.

Consequently, a complete 34+3 native output write would require assumptions
that exceed Astraea's current evidence standard.

Issue #188 therefore implements only a pure request planner. It validates:

- Astraea internal LinkShaders HLE ID 5;
- non-null context and user-config output addresses;
- null hull handle;
- primitive type exactly 4;
- checked `0x110` and `0x18` output ranges;
- non-overlapping output ranges;
- distinct pre-raster and pixel handles;
- exact unique handle lookup;
- type-2 Geometry / `v18_geometry_es_public_shape` in the pre-raster slot;
- type-1 Pixel / `v18_pixel_public_shape` in the pixel slot.

The resulting plan copies stable shader identity facts rather than retaining
registry references. It performs no guest-memory access and no registry
mutation.

## Follow-up

Issue #189 owns evidence-bounded output materialization.

That issue must either obtain stronger evidence for every intended output byte
or explicitly represent a measured partial-write contract that preserves all
unmeasured guest bytes. It must not zero-fill or synthesize the missing
records.

Only after #189 has a justified transactional apply should the generic HLE
runtime return guest-visible LinkShaders success.

## Clean-room boundary

Astraea uses public declarations, public homebrew call sites, and published
measurement descriptions to establish observable contracts, then implements
independent C++ code against Astraea-owned synthetic fixtures.

No Sony firmware, keys, proprietary SDK material, decrypted retail assets, or
third-party shader binaries are committed.


## #189 measured-partial output decision

A deeper evidence sweep before writing any LinkShaders output compared:

- the published Orbistoun/obSCEne measurements;
- the pinned ps5-agc-gears FW 12.02 allocation structures and shader-header
  “specials” inputs;
- generic AMD register maps from pinned Linux sources.

The result strengthens one record but does **not** justify a complete 37-record
write.

### Generic AMD corroboration for the measured routing record

Pinned Linux AMD register maps at
`torvalds/linux@fe2ec83746e501645709761605c2464a44fd2929`
identify `VGT_GS_OUT_PRIM_TYPE` at context register address `0xA29B`.

Under the compact context-offset convention used by the public AGC
offset/value arrays, that corroborates the measured LinkShaders record offset
`0x29b`.

This is family-level register-name corroboration only. The PS5-specific value
`2` remains justified by the hardware measurement, not by desktop defaults.

### Why shader-header “specials” are not copied into output

Pinned `ps5-agc-gears` builds a type-2 shader specials block containing
candidate inputs named GE_CNTL, shader-stages state, output-primitive state,
and a GE_USER_VGPR_EN slot. Those are useful clues about LinkShaders inputs,
but they do not prove the native LinkShaders output transformation.

In particular, one published specials slot uses offset `0x2ce` for a field
named `gs_out_prim_type`, while generic AMD maps and the measured LinkShaders
output identify `VGT_GS_OUT_PRIM_TYPE` at `0x29b`. Astraea therefore does
not treat the specials block as a verbatim output template.

The companion primitive-state measurements likewise do not recover the
missing UC records: they measure topology in a separate secondary-state field,
while another routing word is explicitly described as touched but unmeasured.

### Exact #189 write contract

#189 writes exactly two measured context regions:

1. `context + 0x000 .. +0x0ff`
   - 32 records
   - record `i = {offset 0x191 + i, value i}`
2. `context + 0x108 .. +0x10f`
   - one record
   - `{offset 0x29b, value 2}`

It intentionally preserves:

- `context + 0x100 .. +0x107`;
- all `0x18` user-config output bytes;
- all unrelated bytes before and after both blocks.

The output plan and apply report are explicitly tagged
`measured_partial`. No runtime dispatch branch returns LinkShaders success
from this partial state.

The apply path preflights both measured write ranges before the first guest
mutation. A preflight failure therefore leaves every measured and preserved
byte unchanged.

## Completion follow-up

Issue #191 owns the four missing native records:

- context `+0x100`;
- UC record 0;
- UC record 1;
- UC record 2;

and only after those are measured strongly enough does it wire internal HLE ID
5 into runtime dispatch and the owned SCE end-to-end fixture.
