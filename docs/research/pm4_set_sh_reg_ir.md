# PM4 SET_SH_REG -> Graphics IR

## Purpose

Issue #158 adds the first behavioral PM4 semantic after #156/#157 framing.

The selected workload-driven packet is SET_SH_REG because the public native
PS5 cube path programs shader-register arrays/ranges before draw, and the same
generic packet is independently documented by AMD/Linux.

## Independent AMD evidence

Pinned revision:

`torvalds/linux@fe2ec83746e501645709761605c2464a44fd2929`

Public AMDGPU headers define:

- `PACKET3_SET_SH_REG = 0x76`;
- `PACKET3_SET_SH_REG_START = 0x2c00`;
- `PACKET3_SET_SH_REG_END = 0x3000`.

AMDGPU command construction emits:

`Type-3 header -> (register - SET_SH_REG_START) -> value(s)`.

The first supported Astraea profile therefore uses a relative shader-register
window of 0x400 dwords.

Some public AMD headers expose additional high control fields in the offset
word. Astraea does not guess those meanings. The first profile requires bits
31:16 to be zero and returns a typed error otherwise.

## PS5 workload corroboration

`Rufidj/ps5link-sdk@ea771e535378740b6a058b8e5419eb8a0e0e0ec8`

The public cube path calls both `sceAgcDcbSetShRegistersIndirect` and
`sceAgcCbSetShRegisterRangeDirect` before index/draw submission.

Current public PS5 compatibility research independently reports opcode 0x76
range behavior. That evidence is corroborative only; Astraea's generic packet
semantic comes from AMD definitions.

## Astraea boundary

`lower_pm4_type3_frame_to_graphics_ir()` assigns semantics only to opcode
0x76:

- low 16 bits of body dword 0 -> relative start offset;
- high 16 bits -> unsupported control bits in the first profile;
- encoded Type-3 count -> number of consecutive values;
- ordered value dwords -> `GraphicsIrShaderRegisterWriteRange::values`.

Non-0x76 Type-3 packets remain `GraphicsIrUnsupported`.

The operation does not assign VS/PS/CS stage, register names, resource
descriptors, shader program identity, or Vulkan meaning. Raw packet bytes stay
provenance only.
