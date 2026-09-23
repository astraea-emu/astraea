# PS5 AGC runtime shader-pair evidence

**Accessed:** 2026-09-22  
**Scope:** Raw shader-header + shader-text pairs crossing the runtime
`sceAgcCreateShader` boundary. Corrected by #143 after a byte-level evidence
check of current public shader samples.

This note records only behavior needed to validate the raw guest-domain shader
input before native AGC preparation. It does not claim to describe every AGC
version or every prepared-header field.

## Public evidence

Current public PS5 homebrew paths agree that the outer AGC ELF is a
storage/transport envelope. At runtime its `.shader_header` and
`.shader_text` contents are copied into separate buffers and passed to:

```c
int sceAgcCreateShader(void **outShader, void *shaderHeader, void *gpuCode);
```

Sources used for the boundary and raw/prepared layout:

- SharpProspero `ShaderBinary.cs`
- SharpProspero `AgcShader.cs`
- SharpProspero `Interop/Agc/SceAgc.cs`
- SharpProspero public built-in `mesh_ps.sb` and `mesh_vs.sb` inspected at
  commit `9220876e25bc28aca1f65ea644783a479949ad77` without committing their
  bytes
- ps5link SDK `examples/gpu_cube/main.c` and `shaders/tools/agcpack.py`
- Kyty `GraphicsCreateShader` at commit
  `4733b7e1c91b10554a52007903d74dc76c39a230`
- current prosper `hle_agc.cpp` used only as independent behavioral
  corroboration

The public import identity is independently corroborated as
`f3dg2CSgRKY` for `sceAgcCreateShader` in `libSceAgc`.

## #143 correction: register-list qwords are self-relative

The original #138 note described the qwords at `+0x18` and `+0x20` as
header-absolute list offsets. A deeper check before implementing native-style
header preparation falsified that interpretation for the current public
samples.

For SharpProspero `mesh_ps.sb`:

- header size = `0x160`
- raw qword at `+0x18` = `0xB0`
- raw qword at `+0x20` = `0x78`
- context-register count = 9
- shader-register count = 6

The coherent lists begin at:

- context: `0x18 + 0xB0 = 0xC8`
- shader: `0x20 + 0x78 = 0x98`

Treating `0xB0` and `0x78` as offsets from header byte zero instead lands
inside unrelated/mixed data. The public `mesh_vs.sb` independently has the
same raw qwords and produces coherent lists only under the same field-relative
interpretation.

Kyty's public `GraphicsCreateShader` also prepares these fields by adding
the address of each pointer field to its raw value. Current prosper
independently follows the same observable rule. SharpProspero reads
`+0x18/+0x20` as pointers after native `sceAgcCreateShader` returns.

Therefore, for a non-zero register-list count, Astraea resolves:

```text
list_offset = pointer_field_offset + raw_relative_delta
```

with checked arithmetic before validating the complete list extent.

For a zero-count list, Astraea deliberately does not interpret the raw qword.

## Raw header facts currently typed

| Field | Offset | Treatment |
| --- | ---: | --- |
| magic | `+0x00` | exact `0x34333231` |
| format version | `+0x04` | preserved raw |
| context-register list delta | `+0x18` | self-relative from `+0x18` when count is non-zero |
| shader-register list delta | `+0x20` | self-relative from `+0x20` when count is non-zero |
| declared header size | `+0x40` | validated against supplied header span |
| declared shader-text size | `+0x44` | validated against supplied text span |
| program type | `+0x5a` | known 0..8 typed; unknown values preserved |
| context-register count | `+0x5b` | bounded list count |
| shader-register count | `+0x5c` | bounded list count |

A register-list record is eight bytes: a little-endian 16-bit register offset,
two bytes that Astraea does not assign semantics to, and a little-endian
32-bit value.

The shader-text trailer/program rules remain those documented for #134 in
`docs/research/ps5_agc_shader_container.md`.

## Preparation boundary

Public native wrappers make these facts important for the next V1 slice:

1. the header buffer is writable and is prepared in place;
2. the returned shader handle points at the prepared shader-program header;
3. both header and code storage must remain alive while the shader is used;
4. at least some raw internal pointer fields are relocated during creation.

The corrected raw parser does **not** itself mutate those fields. Preparation
is a later HLE responsibility and must remain field-by-field and
evidence-scoped. The correction in #143 is specifically about locating raw
context/shader register records before preparation; it does not make every
other pointer-bearing AGC field equivalent.

The object produced by this boundary is a **guest-domain AGC shader object**.
It is not a Vulkan shader module, pipeline, descriptor set, or other host GPU
handle.

## Deliberately not decoded

The canonical parser still does not assign semantics to:

- user-data/resource tables;
- descriptors or surface/tiling metadata;
- general prepared-pointer relocation beyond the two register-list deltas
  needed to find raw register records;
- program-address register patching;
- hashes/checksums;
- input/output semantics arrays;
- launch state, wave mode, floating-point MODE;
- command buffers, queues, synchronization, or presentation.

Those remain separate dependencies and are pulled forward only when a vertical
gate needs them.

## Clean-room rule

Astraea uses public projects and public homebrew artifacts to establish
observable layout/call contracts, then implements independent C++ code with
Astraea-owned synthetic bytes. No third-party shader binary, Sony binary,
firmware, key, proprietary SDK file, or copied implementation is committed.


## V1 preparation evidence for #145

The #143 correction was followed by a field-by-field preparation audit before
any guest mutation code was added.

Current public SharpProspero `mesh_ps.sb` at commit
`9220876e25bc28aca1f65ea644783a479949ad77` was inspected without committing
its bytes. The relevant raw shape is:

- header size `0x160`;
- format version `0x18`;
- pixel program type `1`;
- top-level self-relative qwords:
  - user-data `+0x08 = 0x108`, resolving to header `+0x110`;
  - context registers `+0x18 = 0xB0`, resolving to `+0xC8`;
  - shader registers `+0x20 = 0x78`, resolving to `+0x98`;
  - specials `+0x28 = 0x38`, resolving to `+0x60`;
  - input semantics `+0x30 = 0x60`, resolving to `+0x90`;
  - output semantics `+0x38 = 0`.
- the first two shader-register records at the corrected `+0x98` list are
  offsets `0x08` and `0x09`.

Kyty and current prosper independently identify those first two pixel
register offsets as `SPI_SHADER_PGM_LO_PS` and `SPI_SHADER_PGM_HI_PS`, and
both model shader creation as writing code-address bits 8..39 and 40..47 into
their values. Astraea treats that as independently corroborated
emulator/research evidence, not as an official Sony specification.

The same public pixel sample resolves its user-data object to header
`+0x110`. Kyty's published `ShaderUserData` layout and SharpProspero's
post-create resource accessor agree that the object begins with five pointer
fields: one direct-resource pointer followed by four sharp-resource pointers.
For the inspected sample their raw self-relative values resolve to:

- direct-resource pointer: header `+0x148`;
- each of the four sharp-resource pointers: header end `+0x160`.

The latter are one-past-end values paired with zero-count resource kinds in
the inspected profile. #145 therefore permits a non-zero self-relative
pointer target to resolve exactly to the validated header end; it does not
dereference or interpret descriptor payloads.

The creation behavior used by #145 is intentionally split into three layers:

1. canonical raw parsing owns format validation and corrected register-list
   provenance;
2. a pure preparation planner derives only evidenced guest byte patches;
3. an apply step preflights every writable patch range before mutating the
   guest and publishes the output shader handle last.

The supported first profile does not claim universal AGC semantics. Other
shader stages, versions, resource payload interpretation, command buffers, and
host GPU objects remain separate dependencies.


## Exact import identity used by the final V1 probe

The final V1 integration slice uses the public `sceAgcCreateShader` NID
`f3dg2CSgRKY` and an owned SCE-profile guest fixture.

A current public SharpProspero linker shows that the long-form dynamic symbol
suffix is not a fixed spelling of the library and module names. Imports are
encoded as:

```text
<NID>#<encoded-library-id>#<encoded-module-id>
```

where library and module IDs are assigned within the importing image. The
public linker numbers libraries from zero and imported modules from one, then
encodes those numeric IDs with the PS5 long-form alphabet. Consequently, for
an owned fixture in which `libSceAgc` is the first and only imported
library/module:

- library ID 0 encodes as `A`;
- module ID 1 encodes as `B`;
- the exact symbol is `f3dg2CSgRKY#A#B`.

This does **not** mean that `#A#B` universally identifies `libSceAgc`.
Astraea continues to resolve the complete opaque long-form identity exactly.

The same public linker records SCE module/library imports with packed values:

```text
name_string_offset | (version << 32) | (id << 48)
```

and the current public libSceAgc stub/catalog uses:

- module name `libSceAgc`;
- soname `libSceAgc.prx`;
- module version `0x0101`;
- library version `0x0001`.

For the final V1 owned probe, Astraea preserves and checks the corresponding
raw `DT_SCE_NEEDED_MODULE`, `DT_SCE_IMPORT_LIBRARY`, and import-library
attribute records, while the ordinary `DT_NEEDED` string independently
resolves to `libSceAgc.prx`.

This slice deliberately does not add a general decoder from packed SCE
dynamic-record values back to library/module names. The existing loader keeps
those values as evidence until a later dependency requires stronger typed
semantics.


## V3 type-2 Geometry/ES preparation evidence

After the #175 W0-W2 resource substrate proof, #172's offscreen raster path
requires a created pre-raster shader before shader linkage can be represented.

### Program-kind identity

Pinned SharpProspero source:

- repository: `SvenGDK/SharpProspero`
- commit: `9220876e25bc28aca1f65ea644783a479949ad77`
- `src/SharpProspero/Graphics/Agc/AgcShader.cs`
- `src/SharpProspero/Graphics/Agc/Shaders/mesh_vs.pssl`

The public `ShaderKind` identifies raw program type 2 as:

`Geometry shader (or a fused vertex-plus-geometry shader)`.

The public mesh source's source-level vertex entry point is exported as
`[CxxSymbol("Shader::gs")]`. Astraea therefore treats type 2 as the
Geometry/fused-pre-raster family and does not invent a standalone AGC vertex
program kind.

### Existing raw-header shape

The earlier #143 audit already inspected the public SharpProspero
`mesh_vs.sb` at the same pinned commit without committing its bytes. It found
the same v0x18 self-relative context/shader register-list qwords as the public
pixel sample, and coherent lists only under the same field-relative pointer
interpretation.

That supports reusing the already-proven v0x18 pointer/list preparation
machinery for a bounded type-2 profile.

### Type-2 program-address register pair

Pinned Kyty source:

- repository: `InoriRus/Kyty`
- commit: `4733b7e1c91b10554a52007903d74dc76c39a230`
- `source/emulator/src/Graphics/Graphics.cpp`
- `source/emulator/include/Emulator/Graphics/Pm4.h`

Its public `GraphicsCreateShader` explicitly requires, for `h->type == 2`,
that the first two shader-register records are:

```text
SPI_SHADER_PGM_LO_ES = 0xC8
SPI_SHADER_PGM_HI_ES = 0xC9
```

and patches the values from the GPU code address as:

```text
LO = (address >> 8)  & 0xffffffff
HI = (address >> 40) & 0xff
```

Pinned current Prosper source independently corroborates the CreateShader
program-address behavior:

- repository: `mattias800/prosper`
- commit: `51f0a60b49ad68a1a326e3d0673545367776e9f2`
- `prosper/src/hle/graphics/hle_agc.cpp`

Its CreateShader implementation scans the leading shader-register pair against
known PS/ES/GS/HS/compute PGM pairs and applies the same address encoding.
This independently supports ES as a real prepared-program pair and the
stage-independent address encoding.

These are public emulator/research observations, not an official Sony
specification. Astraea therefore supports only the exact type-2 / leading
ES-pair profile required by the owned raster path.

### Astraea profile boundary

The new preparation profile is:

`v18_geometry_es_public_shape`

and requires:

- header version 0x18;
- program type raw 2 / known Geometry;
- existing validated self-relative pointer/list provenance;
- leading shader-register offsets exactly 0xC8 / 0xC9;
- the same 256-byte-aligned, <=48-bit code-address envelope already used by
  the pixel profile;
- stage-specific ES PGM value patches;
- output handle publication last.

All other header bytes remain outside the mutation contract.

This slice does not yet make the real HLE service register a type-2 shader.
Persistent stage-aware materialization/identity and `sceAgcLinkShaders` are
separate dependencies.
