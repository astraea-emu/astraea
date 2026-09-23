# Submitted pixel-shader binding evidence profile

**Accessed:** 2026-09-22  
**Scope:** Issue #169 — the smallest evidence-bounded submission-side proof
that a captured AGC DCB can select one previously created pixel shader.

This note does not claim complete `sceAgcDriverSubmitDcb`, PM4, queue, reset,
resource, draw, or synchronization semantics.

## Existing Astraea evidence

The binding planner composes only already-merged contracts:

- #154/#155 — immutable 16-byte SubmitDcb descriptor plus bounded raw DCB
  capture;
- #156/#157 — generic PM4 Type-3 framing with exact packet provenance;
- #158/#159 — SET_SH_REG (0x76) body semantics for the supported register
  window;
- #160/#161 — explicit shader-register initialization state and the
  evidence-scoped pixel PGM_LO/PGM_HI GPU-address resolver;
- #162/#163 — duplicate-safe created-shader lookup by typed program GPU
  address;
- #164/#165 — transactional registration of real guest-created AGC shaders.

The captured DCB remains the raw byte source of truth.

## Submit descriptor flag

Astraea preserves the descriptor's one-byte `flag` field but has no public
evidence establishing nonzero meanings.

Two independent public PS5 AGC code paths use the captured descriptor shape
and set the field to zero:

- ps5link SDK, pinned commit
  `ea771e535378740b6a058b8e5419eb8a0e0e0ec8`,
  `examples/gpu_cube/main.c`: the submit descriptor is populated with
  `submit.flag = 0` immediately before `sceAgcDriverSubmitDcb`.
- SharpProspero, pinned commit
  `9220876e25bc28aca1f65ea644783a479949ad77`,
  `src/SharpProspero/Graphics/Agc/AgcDevice.cs`: its
  `SubmitDescription` is populated with `Flag = 0` before the same driver
  call.

These observations support a first **flag-zero-only** Astraea profile. They do
not establish what any nonzero flag means. Nonzero values therefore fail with
a typed unsupported-profile error.

## Type-3 low control byte

The generic Type-3 framer preserves the header's low control byte exactly.
Astraea does not currently have evidence assigning guest behavior to nonzero
values in that byte for this submission path.

SET_SH_REG semantics in #158/#159 are independently established from the
packet body. That does not authorize ignoring unknown header control bits.

The first binding profile therefore accepts only
`Pm4Type3Header::low_control_bits == 0`. Any nonzero value fails explicitly.

This is a conservative support boundary, not a claim that hardware rejects
nonzero values.

## Shader-register state profile

The first owned binding fixture starts with a fresh
`ShaderRegisterState`.

The submitted DCB itself must establish both:

- relative shader-register offset `0x08` — pixel PGM_LO;
- relative shader-register offset `0x09` — pixel PGM_HI.

This deliberately avoids inventing cross-submission reset/inheritance
semantics.

For every successful SET_SH_REG write, source ordering is preserved. If a
later packet rewrites PGM_LO or PGM_HI, the later value is the effective state
and its source becomes the effective provenance.

For a SET_SH_REG value at zero-based `value_index`, the exact source dword in
the captured DCB is:

```text
frame.word_offset + 2 + value_index
```

because word 0 is the Type-3 header and word 1 is the offset/control word.

## Created-shader identity

The final PGM_LO/PGM_HI pair is resolved through the existing typed
`PixelProgramGpuAddress` contract.

That value is a **guest GPU-domain address**, not a CPU guest pointer, host
pointer, Vulkan handle, or `VkDeviceAddress`.

The existing `CreatedAgcShaderRegistry::lookup_unique` contract is reused:

- zero matches -> explicit not-found;
- one match -> selected shader;
- multiple matches -> explicit ambiguous result.

The binding planner does not impose a new uniqueness rule.

## Unsupported behavior remains explicit

This slice does not:

- wire `sceAgcDriverSubmitDcb` into HLE dispatch;
- return guest-visible SubmitDcb success;
- silently skip unknown/non-SET_SH_REG Type-3 packets;
- interpret draw/dispatch/flip packets;
- infer queue or register reset behavior;
- decode buffers, images, samplers, descriptors, or surfaces;
- execute or compile the selected shader;
- call Vulkan;
- assign semantics to nonzero submit flags or nonzero Type-3 low-control bits.

Those remain future dependency/evidence questions.

## Clean-room rule

The public projects above establish observable descriptor usage only. Astraea's
planner, types, tests, and synthetic command streams are independently written
and contain no third-party implementation code, proprietary Sony SDK material,
firmware, keys, or retail assets.
