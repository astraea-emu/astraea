# Project Status

**Milestone:** M4 — Platform/HLE expansion  
**State:** Imported-function relocation orchestration baseline complete; bounded scalar Shader IR programs, explicit V_MOV_B32 wave execution, and mixed scalar/vector one-block execution complete; bounded mixed-wave program execution in progress  
**Repository:** astraea-emu/astraea  
**Active branch:** `feat/m4-shader-wave-program-execution`

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
- Raw graphics packet/header preservation slice with synthetic fixtures (#29).
- Evidence-backed SCE dynamic metadata classification/preservation (#30).
- Minimal generic RDNA2 SOPP decoder from AMD document 70648 (#31).
- Minimal host-independent Graphics IR semantic/provenance boundary (#32).
- Minimal host-independent Shader IR for the AMD-documented SOPP subset (#33).
- Graphics/frontend/shader Trace v0 adapters with semantic/provenance separation (#34).
- Opaque SCE long-form dynamic-symbol identity parser (#43).
- Exact opaque SCE identity -> HLE function binding registry (#45).
- Validated dynamic symbol -> exact raw spelling / optional SCE identity materialization (#47).
- Validated relocation + exact SCE identity + exact HLE binding resolution plan (#50).
- Portable x86-64 R_X86_64_JUMP_SLOT synthetic gate patch builder (#52).
- Explicit opt-in PS5/SCE ELF parse profile for 0xFE10 / 0xFE18 (#54).
- Validated synthetic JUMP_SLOT patch application through GuestMemoryAccess (#56).
- Owned SCE-profile ELF end-to-end import/execution proof through HLE exit 42 (#57).
- Evidence-backed SCE program-header vocabulary classification/preservation (#61).
- Two exact SCE imports executed end to end through write → HLE resume → exit (#63).
- Ordered exact SCE PLT import batch planning with indexed typed failures (#66).
- Ordered validated x86-64 JUMP_SLOT batch patch construction with explicit gate slots (#68).
- Ordered non-atomic JUMP_SLOT batch application with indexed partial-failure reporting (#70).
- Evidence-backed x86-64 R_X86_64_GLOB_DAT imported-function gate patch construction (#72).
- Validated GLOB_DAT gate patch application through GuestMemoryAccess (#74).
- Owned SCE-profile general-RELA GLOB_DAT import/execution proof through HLE exit 42 (#76).
- Table-kind-neutral ordered exact-SCE batch import planning with PLT compatibility wrapper (#79).
- Ordered validated x86-64 GLOB_DAT batch patch construction with explicit gate slots (#81).
- Ordered non-atomic GLOB_DAT batch application with indexed partial-failure reporting (#83).
- AMD-documented RDNA2 SOPP conditional branches 4-9 lowered to typed Shader IR and Trace semantics (#85).
- AMD-documented RDNA2 SOPP S_BARRIER lowered to a typed workgroup-barrier Shader IR/Trace marker (#87).
- AMD-documented RDNA2 SOPP S_WAITCNT thresholds lowered to typed Shader IR/Trace semantics (#88).
- AMD-documented RDNA2 SOP1 S_MOV_B32 plain SGPR moves lowered to typed Shader IR/Trace semantics (#92).
- AMD-documented RDNA2 SOP1 S_MOV_B64 plain even-aligned SGPR-pair moves lowered to typed Shader IR/Trace semantics (#94).
- AMD-documented RDNA2 S_MOV_B32 single-word integer inline sources lowered to typed Shader IR/Trace semantics (#95).
- AMD-documented RDNA2 S_MOV_B32 selector-255 literal extension lowered to typed Shader IR/Trace semantics (#98).
- AMD-documented non-privileged VCC_LO/VCC_HI/M0/NULL/EXEC_LO/EXEC_HI S_MOV_B32 sources typed in Shader IR/Trace (#100).
- AMD-documented RDNA2 VOP1 V_MOV_B32 plain VGPR-to-VGPR moves lowered to typed Shader IR/Trace (#102).
- AMD-documented RDNA2 VOP2 V_ADD_F32 plain VGPR + VGPR -> VGPR arithmetic lowered to typed Shader IR/Trace semantics (#104).
- Bounded whole-stream generic RDNA2 decode/lower pipeline with validated variable instruction extents (#106).
- Validated Shader IR basic-block/control-flow graph with typed branch and fallthrough edges (#108).
- Shader CFG block/edge topology exposed through stable Trace v0 semantics and first-divergence comparison (#110).
- Explicit generic RDNA2 scalar state executes the already-typed S_MOV_B32/B64 Shader IR forms with traced SGPR write effects (#112).
- Explicit SCC/VCC/EXEC state evaluates the six already-typed conditional branch predicates with traced taken/not-taken decisions (#114).
- Validated Shader CFG exits select deterministic successor/terminal outcomes from matching conditional decisions (#117).
- One validated Shader IR basic block executes supported scalar operations and returns its selected successor with explicit non-atomic failure progress (#119).
- Bounded cross-block scalar Shader IR programs execute from an explicit entry block under an explicit block-execution budget (#122).
- Plain VGPR-to-VGPR V_MOV_B32 executes over explicit caller-selected wave32/wave64 state under EXEC with traced lane-write semantics (#124).
- One validated Shader IR block composes scalar moves and plain V_MOV_B32 effects in source order with explicit mixed-state failure progress (#126).
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #128 — execute bounded mixed scalar/vector Shader IR programs by repeatedly composing the validated mixed one-block executor — in progress on this branch.
2. Entry block and maximum block-execution count are explicit caller inputs; scalar/vector state mutations and completed mixed-block provenance remain non-atomic and observable on later failure or budget exhaustion.
3. `V_ADD_F32`, floating-point mode semantics, barriers/waits, Sony shader container/launch ABI, SPIR-V/Vulkan lowering, new opcodes, and `R_X86_64_RELATIVE` remain separately deferred.

## SCE metadata boundary

The #30 slice is additive to the generic dynamic parser:

- classify only current SCE dynamic-tag values documented by #8;
- preserve raw tag/value/source index for every dynamic entry;
- preserve generic, legacy, and otherwise unknown values as `unknown`;
- reject contradictory values for file-global singleton records deterministically;
- keep repeatable module/library records repeatable;
- do not decode undocumented module/library bit packing;
- do not generate NIDs, resolve imports, bind HLE, or guess system-library names.

The existing strict dynamic parser remains responsible for segment bounds, entry
size, arithmetic, and terminator validation.

## SCE program-header boundary

The #61 slice is data-only:

- classify only `0x61000000`, `0x61000001`, and `0x61000010` from the #8 evidence map;
- preserve the complete parsed `ProgramHeader` and source index;
- keep generic and unsupported values typed as `unknown`;
- do not infer mapping permissions, process-parameter layout, loader ordering, RELRO transition timing, entry ABI, or dependency behavior.

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

Validate #128 across the five-gate matrix. Repeatedly compose the merged mixed
scalar/V_MOV_B32 block executor across already-selected CFG successors under an
explicit entry block and block-execution budget, preserving completed mixed-block
progress on failure while keeping V_ADD_F32, PS5 launch ABI, SPIR-V, and Vulkan out.
