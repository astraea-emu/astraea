# Project Status

**Milestone:** M3 — Behavioral evidence and differential tooling  
**State:** Trace v0 and deterministic first-divergence tooling complete; RDNA2/PS5 graphics evidence map in review  
**Repository:** astraea-emu/astraea  
**Active branch:** `research/rdna2-ps5-graphics-evidence`

## Complete

- M0 engineering foundation.
- M1 validated `GuestImage`.
- M2 controlled execution on Linux x86-64 and Windows x86-64 for trusted Astraea-owned synthetic probes.
- Astraea-owned `probe_hello.elf` end-to-end proof.
- Public PS5 executable/module ABI evidence map (#8).
- AstraeaProbe v0 request/result contract and deterministic host reference probe (#10).
- Stable Astraea Trace v0 schema, normalization, and canonical serializer (#6).
- Deterministic Trace v0 diff / first-divergence locator (#7).
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #9 — RDNA2/PS5 graphics evidence map — in review on this branch.
2. Create a narrow graphics-frontend issue from #9: typed packet/header/raw-preservation infrastructure using synthetic fixtures only.
3. Continue the data-only SCE metadata parser implied by #8.
4. Create a generic RDNA2 instruction-decoder issue using only AMD-published ISA fixtures.
5. Freeze minimal Graphics IR and Shader IR contracts before a Vulkan backend.
6. Add Trace v0 adapters at frontend/IR boundaries before compatibility-driven graphics work.

## #9 scope

- official PS5 hardware baseline versus unsupported desktop-Radeon assumptions
- public generic RDNA2 ISA facts
- host SPIR-V/Vulkan/compiler constraints and precedents
- pinned public PS5-oriented community observations with explicit confidence limits
- command/state frontend questions
- shader-container versus shader-microcode separation
- resource descriptor and surface-layout unknowns
- synchronization/coherency unknowns
- candidate Graphics IR and Shader IR boundaries
- controlled synthetic experiments that can falsify assumptions
- explicit architecture rule: guest semantics first, Vulkan lowering later

The evidence map does not copy another emulator's GPU implementation and does
not claim that PS5 is exactly equivalent to a desktop `gfx1030` device.

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

Validate #9 as a documentation-only evidence slice. After merge, turn its
architecture conclusions into separate narrow issues rather than starting a
monolithic GPU implementation.
