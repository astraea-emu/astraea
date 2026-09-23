# Project Status

**Integration gate:** PS5 graphics frontend after V3  
**State:** V1/V2/V3 proof chain complete; created-shader registry complete; transactional sceAgcCreateShader registration active  
**Repository:** astraea-emu/astraea  
**Active branch:** `feat/v1-register-created-shaders-transactionally`

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
- Corrected self-relative AGC context/shader register-list addressing is complete and five-gate validated (#143/#144).
- Evidence-backed version-0x18 pixel shader-object preparation and all-write preflight/application are complete and five-gate validated (#145/#147).
- Owned SCE-profile guest execution of the real `sceAgcCreateShader` import through generic HLE dispatch, guest shader-object preparation, `RAX=0` resume, and controlled exit is complete and five-gate validated (#148/#149). V1 is complete.
- Deterministic Shader IR -> Vulkan-valid SPIR-V 1.6 register-state compute-probe lowering for NOP/V_MOV_B32/V_ADD_F32/END is complete and five-gate validated (#150/#151).
- Headless Vulkan execution of that V2 probe is complete and five-gate validated (#152/#153); Linux CI forces Mesa Lavapipe and proves bit-for-bit readback equality with Astraea's existing interpreter. V3's first actual host-GPU semantic proof is complete.
- Immutable `sceAgcDriverSubmitDcb` guest-memory capture is complete and five-gate validated (#154/#155): exact 16-byte descriptor plus bounded raw DCB stream, with no guest writes, PM4 decode, fake success, or Vulkan dispatch.
- Generic AMD PM4 Type-3 framing of captured DCB streams is complete and five-gate validated (#156/#157), preserving opcode/control fields structurally and exact RawPacket provenance without assigning opcode behavior.
- Workload-driven generic AMD SET_SH_REG (0x76) lowering to typed consecutive relative shader-register Graphics IR is complete and five-gate validated (#158/#159), with unsupported control bits and out-of-window ranges failing explicitly.
- Persistent shader-register state with explicit initialized-vs-zero semantics plus evidence-scoped pixel PGM_LO/PGM_HI GPU-address reconstruction is complete and five-gate validated (#160/#161).
- Created pixel shader materialization plus duplicate-safe not-found/unique/ambiguous lookup by typed program GPU address is complete and five-gate validated (#162/#163).
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. V1 is complete through #148/#149.
2. V2's first deterministic SPIR-V backend is complete through #150/#151.
3. V3's first actual host-GPU proof is complete through #152/#153.
4. #154/#155 capture a real SubmitDcb descriptor/stream.
5. #156/#157 frame that stream as generic AMD PM4 Type-3 packets.
6. #158/#159 lower SET_SH_REG to typed shader-register Graphics IR.
7. #160/#161 persist shader-register state and reconstruct the existing pixel program GPU address.
8. #162/#163 materialize validated created pixel shaders and provide duplicate-safe lookup by that address.
9. #164 is active: integrate created-shader registration transactionally into the real sceAgcCreateShader HLE path, rolling back the exact temporary record if guest preparation fails.
10. Submission-side shader binding, draw/dispatch semantics, resource meaning, other shader stages, and real guest-resource Vulkan execution remain later dependencies.

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

Finish #164 on `feat/v1-register-created-shaders-transactionally`.

The service transaction is:

`plan create -> plan preparation -> materialize record -> register record -> apply guest patches -> resume`.

If guest application fails, roll back exactly the just-added final registry entry before surfacing the existing apply failure. Materialization and registration failures must happen before guest publication and preserve typed nested provenance.

Do not process SubmitDcb in HLE runtime, resolve submitted pixel state here, add destruction/lifetime APIs, decode resources/descriptors, interpret draw packets, call Vulkan, or add RDNA2 instructions.

After merge, build the smallest owned submission-side bridge:

`captured DCB -> Type-3 framing -> SET_SH_REG IR -> persistent shader state -> pixel program address -> unique created-shader lookup`.
