# PS5 AGC runtime shader-pair evidence

**Accessed:** 2026-09-22  
**Scope:** Issue #138 — the raw shader-header + shader-text pair that crosses
the runtime `sceAgcCreateShader` boundary.

This note records only behavior needed to validate the raw guest-domain shader
input before native AGC preparation. It does not claim to describe every AGC
version or every prepared-header field.

## Public evidence

Current public, independently implemented PS5 homebrew paths agree that the
outer AGC ELF is a storage/transport envelope. At runtime its
`.shader_header` and `.shader_text` contents are copied into separate
buffers and passed to:

```c
int sceAgcCreateShader(void **outShader, void *shaderHeader, void *gpuCode);
```

Sources used for the boundary and raw layout:

- SharpProspero `ShaderBinary.cs`
- SharpProspero `ShaderInfo.cs`
- SharpProspero `AgcShader.cs`
- SharpProspero `Interop/Agc/SceAgc.cs`
- ps5link SDK `examples/gpu_cube/main.c`
- ps5link SDK `shaders/tools/agcpack.py`

The public import identity is independently corroborated as
`f3dg2CSgRKY` for `sceAgcCreateShader` in `libSceAgc`. Issue #138 does
not bind or dispatch that import; the identity is recorded for the following
V1 HLE slice.

## Raw header facts used by #138

Before native preparation, current public code treats the following fields as
header-relative metadata:

| Field | Offset | Treatment |
| --- | ---: | --- |
| magic | `+0x00` | exact `0x34333231` |
| format version | `+0x04` | preserved raw |
| context-register list byte offset | `+0x18` | bounded header-relative offset |
| shader-register list byte offset | `+0x20` | bounded header-relative offset |
| declared header size | `+0x40` | validated against supplied header span |
| declared shader-text size | `+0x44` | validated against supplied text span |
| program type | `+0x5a` | known 0..8 typed; unknown values preserved |
| context-register count | `+0x5b` | bounded list count |
| shader-register count | `+0x5c` | bounded list count |

A register-list record is eight bytes: a little-endian 16-bit register offset,
two bytes that Astraea does not assign semantics to, and a little-endian
32-bit value.

For a zero-count list, Astraea deliberately does not interpret the raw offset
field. For a non-zero list, the complete `offset + count * 8` extent must
remain within the raw header. This is an input-validation rule, not a claim
about prepared pointer semantics.

The shader-text trailer/program rules remain those documented for #134 in
`docs/research/ps5_agc_shader_container.md`.

## Preparation boundary

Public native wrappers make two facts important for the next V1 slice:

1. the header buffer is writable and is prepared in place;
2. the returned shader handle points at the prepared shader-program header,
   while both header and code storage must remain alive.

Public research also indicates that preparation relocates internal
header-relative fields and associates the code address with the shader.
Astraea does **not** implement those mutations in #138. They must be encoded
only in a later bounded HLE slice with explicit evidence and tests.

The object produced by this boundary is a **guest-domain AGC shader object**.
It is not a Vulkan shader module, pipeline, descriptor set, or other host GPU
handle.

## Deliberately not decoded

Issue #138 does not assign semantics to:

- user-data/resource tables;
- descriptors or surface/tiling metadata;
- header pointer relocation after preparation;
- program-address register patching;
- hashes/checksums;
- input/output semantics arrays;
- launch state, wave mode, floating-point MODE;
- command buffers, queues, synchronization, or presentation.

Those remain separate dependencies and are pulled forward only when a vertical
gate needs them.

## Clean-room rule

Astraea uses the public projects above to establish observable layout and call
contracts, then implements independent C++ code with Astraea-owned synthetic
bytes. No third-party shader binary, Sony binary, firmware, key, proprietary
SDK file, or copied parser implementation is committed.
