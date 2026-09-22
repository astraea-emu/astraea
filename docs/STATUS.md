# Project Status

**Integration gate:** V1 — Guest-created shader object  
**State:** V1 correctness correction active after guest-call materialization  
**Repository:** astraea-emu/astraea  
**Active branch:** `fix/v1-agc-self-relative-register-lists`

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
- Bounded mixed scalar/vector Shader IR programs traverse validated CFG successors under an explicit caller-selected block budget (#128).
- Mode-independent exact finite-normal V_ADD_F32 lane cases execute with integer-only semantics, atomic active-lane prevalidation, and Trace v0 effects (#130).
- Exact V_ADD_F32 execution composes with mixed scalar/vector block execution and bounded CFG traversal while preserving ordered effects and typed nested failures (#132).
- Evidence-backed PS5 AGC shader-container ingestion validates the bounded container envelope, preserves opaque provenance, extracts the documented RDNA2 program prefix, feeds the existing RDNA2 -> Shader IR path end to end, and executes a dedicated fuzz smoke in CI (#134/#135).
- Dependency-driven vertical integration and the V0–V5 architecture gates are adopted as the project planning model (ADR 0006, #136/#137).
- Canonical guest `sceAgcCreateShader` call materialization and backend-neutral guest-memory validation are complete (#140/#141).
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. #140/#141 are complete: RDI/RSI/RDX at the real `sceAgcCreateShader` ABI can be materialized safely from guest memory into the canonical `AgcShaderBinary` without mutation.
2. A pre-preparation evidence audit found that #138/#139 interpreted the raw register-list qwords at `+0x18/+0x20` incorrectly as header-absolute offsets.
3. #143 is active: correct those two fields to checked self-relative deltas from their own qword field addresses and regression-lock the behavior with Astraea-owned bytes shaped after two current public observations.
4. #142 is superseded and closed; its intermediate header-base-offset preparation premise must not be used.
5. After #143, resume V1 with a field-by-field `sceAgcCreateShader` preparation profile, then real service dispatch and the owned end-to-end guest return proof.
6. V2 begins immediately after V1 with the smallest workload-driven Shader IR -> valid SPIR-V proof; V3 then introduces the first guest-driven Vulkan command/resource/result path.

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

Guest semantics come first. Raw guest packets are not Vulkan objects, Sony shader-container bytes are not generic RDNA2 instruction semantics, and PS5 must not be assumed to equal desktop `gfx1030`. "Generic RDNA2" means AMD-defined guest ISA semantics shared by the hardware family; it is not placeholder behavior.

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

Finish #143 and merge only after its immutable PR head passes all five public CI gates, including the AGC shader-binary fuzz smoke.

The correction is intentionally narrow:

`raw register-list qword -> checked (field offset + raw delta) -> bounded 8-byte register records`.

Do not add shader-header mutation, resource-table relocation, program-address patching, command submission, new RDNA2 opcodes, SPIR-V, or Vulkan in #143.

After #143 merges, open a fresh evidence-scoped V1 preparation issue from the corrected semantics. The target remains:

`owned SCE program -> exact libSceAgc import -> sceAgcCreateShader -> prepared guest shader object -> validated AGC/RDNA2/Shader IR -> return to guest`.

That V1 proof closes before host rendering begins. V2 then starts with a minimal Shader IR -> SPIR-V semantic proof, and V3 connects the first controlled guest GPU workload to Vulkan headlessly.
