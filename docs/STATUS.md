# Project Status

**Milestone:** M3 — Behavioral evidence and differential tooling  
**State:** M2 controlled execution complete; M3 evidence, probe, trace, and graphics research foundations complete; narrow implementation slices open  
**Repository:** astraea-emu/astraea  
**Active branch:** `docs/m3-status-after-graphics-evidence`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 controlled execution on Linux x86-64 and Windows x86-64 for trusted Astraea-owned synthetic probes.
- Astraea-owned `probe_hello.elf` end-to-end proof.
- Public PS5 executable/module ABI evidence map (#8).
- AstraeaProbe v0 request/result contract and deterministic host reference probe (#10).
- Stable Astraea Trace v0 schema, normalization, and canonical serializer (#6).
- Deterministic Trace v0 diff / first-divergence locator (#7).
- RDNA2/PS5 graphics evidence map (#9).
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #29 — first graphics frontend slice: typed packet/header parsing with raw-word preservation and synthetic fixtures only.
2. #30 — continue evidence-backed, data-only SCE metadata parsing derived from #8.
3. #31 — minimal generic RDNA2 instruction decoder using AMD-published ISA fixtures only.
4. #32 — freeze the minimal host-independent Graphics IR contract.
5. #33 — freeze the minimal host-independent Shader IR contract.
6. #34 — add Trace v0 adapters at graphics frontend/IR boundaries after the relevant contracts exist.

## Graphics architecture guardrails

The completed #9 evidence map supports separating:

- PS5 command/state frontend
- Graphics IR
- shader-container parsing
- generic RDNA2 instruction decoding
- Shader IR
- SPIR-V lowering
- Vulkan host backend

Guest semantics come first. Raw guest packets are not Vulkan objects, Sony shader-container bytes are not generic RDNA2 instruction semantics, and PS5 must not be assumed to equal desktop `gfx1030`.

Unknown packet/register, shader-ABI, descriptor, surface-layout, synchronization, queue, presentation, and ray-tracing behavior remains explicitly unsupported until stronger evidence or controlled observations justify it.

## Execution boundary

Native execution remains limited to trusted Astraea-owned synthetic probes.

Astraea does **not** currently claim PlayStation 5 software compatibility, and
arbitrary retail guest execution is not enabled.

## Clean-room boundary

Do not commit Sony firmware, Sony keys, proprietary SDK material, decrypted
retail game assets, copyrighted PS5 executables, proprietary system modules,
DRM-bypass material, or unrelated employer/proprietary material.

PS5-specific assumptions require documented evidence.

## Next action

Start #29 as the first graphics implementation slice while #30 can continue independently.
Keep both narrow, typed, deterministic, and synthetic. Do not begin a Vulkan backend
until the frontend and minimal IR contracts are proven.
