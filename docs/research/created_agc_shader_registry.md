# Created AGC shader registry bridge

## Purpose

Issue #162 connects two already-proven Astraea boundaries without adding new
guest semantics:

- V1 shader creation knows the validated AGC object and the code address that
  the pixel PGM_LO/PGM_HI pair is prepared from;
- #160/#161 reconstruct that same pixel program GPU address from submitted
  SET_SH_REG state.

The bridge gives Astraea a deterministic way to map the submitted program
address back to a created shader payload.

## Address-domain rule

The registry stores both:

- the original guest shader-text/code address from `sceAgcCreateShader`; and
- a typed `PixelProgramGpuAddress` with the same numeric value for the
  existing `v18_pixel_public_shape` profile.

This conversion is not generalized to other shader stages or unknown AGC
profiles.

The resulting value is still a GPU-domain identity for frontend matching. It
is not a host pointer and is not a Vulkan shader object.

## Payload rule

Materialization consumes the already validated
`SceAgcShaderPreparationPlan`. It does not re-read guest memory or reparse the
AGC header.

The canonical `AgcShaderBinary::rdna2_words` are lowered once through the
existing `lower_rdna2_stream_to_shader_ir()` path. A decoder/lowering failure
is preserved as nested `ShaderIrProgramError` provenance.

The created record therefore carries:

- guest handle/header/text provenance;
- canonical AGC binary;
- generic RDNA2 words through that AGC binary;
- existing lowered Shader IR.

## Duplicate-address rule

Program GPU address is not treated as a globally unique object key.

The registry preserves every created object in insertion order. Unique lookup
has three explicit outcomes:

- no record -> not_found;
- one record -> unique record;
- more than one record -> ambiguous with exact match count.

No duplicate is overwritten and no arbitrary record is selected.

Logical insertion indices are stable registry identities. A reference returned
by lookup is only valid until the next registry mutation because underlying
storage may move.

## Scope boundary

This slice does not modify real HLE dispatch, mutate guest memory, resume
SubmitDcb, process a whole command stream, decode resources/descriptors, read
guest code by GPU address, or call Vulkan.

The next dependency is rollback-safe integration of this registry with the real
`sceAgcCreateShader` service, followed by the smallest submission-side lookup
bridge from persistent pixel shader-register state.


## Transactional HLE integration

Issue #164 moves the pure registry into the real `sceAgcCreateShader` service.

The ordering is intentionally:

1. validate/materialize all immutable host state;
2. register the created shader;
3. apply the already-preflighted guest preparation patches;
4. return success only after both host and guest state agree.

Registering after guest publication is unsafe because host allocation failure
could leave a guest-visible handle with no host-side object.

The temporary registration is therefore inserted before guest mutation. If
guest application fails, Astraea removes only the exact final logical
registration index with a no-throw `pop_back`-style rollback. A stale or
non-final rollback request is rejected rather than deleting another object.

Shader IR lowering is also moved ahead of guest mutation through
`materialize_created_agc_shader`. A valid AGC envelope whose RDNA2 stream
cannot be lowered therefore fails before either guest publication or registry
mutation.


## V3 stage-aware created-shader identity

Issue #186 extends the persistent created-shader bridge after the bounded
type-2 Geometry/fused-pre-raster preparation profile from #184.

The original V1 record stored a `PixelProgramGpuAddress` as its universal
program identity because only pixel shaders could be materialized. That is no
longer a sound object model once type 2 can be created.

The persistent record now separates:

- backend-independent guest GPU code identity:
  `GpuVirtualAddress code_address`;
- parsed AGC stage:
  `AgcShaderStage stage`;
- the exact supported preparation profile;
- guest shader handle/header/text call provenance;
- canonical parsed `AgcShaderBinary`;
- semantic `ShaderIrProgram`.

The code address remains numerically derived from the validated shader-text GPU
address used by CreateShader preparation, but it is not stored as a pixel
register-domain type. It is neither a CPU guest pointer nor a Vulkan address.

Materialization supports exactly:

- `v18_pixel_public_shape` paired with raw type 1 / known Pixel;
- `v18_geometry_es_public_shape` paired with raw type 2 / known Geometry.

A supported profile paired with the wrong parsed stage is rejected before
registry mutation or guest publication.

### Pixel submission compatibility

The existing `lookup_unique(PixelProgramGpuAddress)` API remains the
submission-side compatibility boundary. It considers only records whose
persisted stage is Pixel and compares the numeric generic GPU code address with
the requested pixel program address.

Therefore a Geometry record with the same numeric code address cannot make a
pixel lookup ambiguous. Duplicate Pixel records still produce the original
explicit ambiguous result.

### Handle identity for linkage

The registry adds duplicate-safe lookup by exact guest shader handle. Handle
lookup is stage-independent and reports not-found/ambiguous outcomes with the
requested handle and exact match count.

Registration remains append-only. The registry does not invent a uniqueness
constraint for guest handles; duplicates are preserved as evidence and fail
lookup explicitly.

### Transactional HLE consequence

No new stage-specific CreateShader service path is added. The existing
transaction remains:

1. capture and validate raw CreateShader input;
2. plan stage/profile-specific preparation;
3. materialize immutable host-side created-shader state;
4. append the registry record;
5. apply all guest preparation patches transactionally;
6. roll back only the just-added final record if guest apply fails.

With #186, that same transaction can persist both the proven Pixel and type-2
Geometry/fused-pre-raster profiles.

This slice still does not implement `sceAgcLinkShaders`, submitted ES/Geometry
program binding, draw semantics, stage I/O, graphics SPIR-V, or Vulkan raster
execution.
