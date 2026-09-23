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
