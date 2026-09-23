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
