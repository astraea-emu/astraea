# PS5 AGC shader-container evidence

**Accessed:** 2026-09-22  
**Scope:** Issue #134 — the smallest public, clean-room envelope needed to
extract a bounded RDNA2 program from a PS5 AGC shader container.

This note records observable format facts. It is not a claim that the current
public samples describe every AGC shader-binary version used by every PS5
title or firmware.

## Sources

### AMD / ELF substrate

- LLVM AMDGPU backend documentation:
  https://llvm.org/docs/AMDGPUUsage.html
- AMD RDNA 2 ISA reference is the semantic source used by Astraea's existing
  generic RDNA2 decoder.

The LLVM documentation identifies `EM_AMDGPU` as 224 and documents 64-bit,
little-endian ELF for the `amdgcn` architecture.

**Confidence:** high for the generic AMDGPU ELF facts.

### Public PS5 AGC shader containers

- ps5link SDK:
  https://github.com/Rufidj/ps5link-sdk
- ps5link AGC packer:
  https://github.com/Rufidj/ps5link-sdk/blob/master/shaders/tools/agcpack.py
- SharpProspero shader loader:
  https://github.com/SvenGDK/SharpProspero/blob/main/src/SharpProspero/Graphics/Agc/ShaderBinary.cs
- SharpProspero shader inspector:
  https://github.com/SvenGDK/SharpProspero/blob/main/tools/SharpProspero.Prx/ShaderInfo.cs
- SharpProspero prepared-shader wrapper:
  https://github.com/SvenGDK/SharpProspero/blob/main/src/SharpProspero/Graphics/Agc/AgcShader.cs

ps5link documents that AMD `gfx1030` assembly is assembled into machine code,
packed into AGC shader containers, passed to `sceAgcCreateShader`, and used by
a GPU-rendered PS5 homebrew title. Its packer also checks that repacking the
public base container reproduces it byte-for-byte before replacing the
program.

SharpProspero independently exposes the same two named ELF sections and
passes the extracted header/code blocks to `sceAgcCreateShader`.

**Confidence:** high that the named header/text envelope is a real,
hardware-accepted AGC path; medium that the currently observed trailer layout
is universal across all AGC versions.

## Fields encoded by #134

The first Astraea parser profile encodes only these facts:

| Fact | Evidence | Treatment |
| --- | --- | --- |
| Container is ELF64 little-endian | LLVM + current public AGC containers | validated |
| ELF machine is `EM_AMDGPU` (224) | LLVM + current public AGC containers | validated |
| `.shader_header` exists as file-backed data | SharpProspero + ps5link samples | validated |
| `.shader_text` exists as file-backed data | SharpProspero + ps5link samples | validated |
| AGC header magic is `0x34333231` | SharpProspero | validated |
| Header version is the dword at `+0x04` | SharpProspero | preserved raw |
| Declared header size is the dword at `+0x40` | SharpProspero + ps5link mutation path | validated against section size |
| Declared shader-text size is the dword at `+0x44` | SharpProspero + ps5link mutation path | validated against section size |
| Program type is byte `+0x5a` | SharpProspero | typed for known 0..8 values; unknown raw values preserved |
| Shader-text trailer is `0x30` bytes in the currently evidenced layout | ps5link packer | used only by this bounded profile |
| Program byte length is trailer `+0x14` | ps5link packer | validated and used to bound the RDNA2 prefix |
| `sl00` byte length is trailer `+0x1c` | ps5link packer | preserved as raw trailer metadata only |

The program prefix must fit wholly before the trailer and be an integral
number of 32-bit dwords before Astraea exposes it to the generic RDNA2
decoder.

## Deliberately *not* encoded yet

The current parser does not assign semantics to:

- ELF `e_type`, OSABI, ABI version, or AGC header version values beyond
  preserving them where useful;
- hashes/checksums in the AGC header or trailer;
- the semantic contents of the `sl00` block;
- context/shader register arrays;
- resource slot tables;
- descriptors, surfaces, tiling, user-data layout, or launch state;
- wave mode, floating-point MODE, queue/command-buffer semantics, or
  synchronization;
- any relationship between an unknown header byte and a Sony SDK type merely
  because the value appears stable in a sample.

Those fields require stronger public evidence or controlled observation before
they become emulator behavior.

## Clean-room implementation rule

Astraea uses the public projects above to establish *what observable layout and
behavior exists*. The implementation in #134 is independent C++ code with
Astraea-owned synthetic ELF fixtures. No third-party shader binary, Sony
binary, firmware, key, SDK file, or copied parser implementation is committed.

## Architectural boundary

The resulting flow is:

    AGC ELF/container envelope
        -> bounded RDNA2 dwords
        -> generic RDNA2 decoder
        -> Shader IR

AGC container metadata remains outside Shader IR. Generic RDNA2 semantics are
not placeholders: they model the AMD-defined guest ISA. Conversely, AGC
container bytes are not treated as if they were generic ISA semantics.
