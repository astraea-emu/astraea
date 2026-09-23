# Project Status

**Integration gate:** V3 — submitted guest GPU state/resources -> Vulkan -> deterministic result  
**State:** V0/V1/V2 complete; V3 active; #175 WRITE_DATA W0-W2 resource-substrate proof complete; next dependency must be re-selected from #172 offscreen raster workload  
**Repository:** astraea-emu/astraea  
**In-flight work:** inspect live open GitHub PRs/issues; this file describes the expected merged frontier on `main`.

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
- Transactional created-shader registration in the real `sceAgcCreateShader` HLE path is complete and five-gate validated (#164/#165), including typed materialization/registration failures and exact-last-entry rollback if guest publication fails.
- Post-V1/V2/V3 architecture synchronization and the separate semantic-IR/compiler-IR contract are complete (#166/#167, ADR 0007).
- Evidence-bounded submitted pixel-shader binding is complete (#169): flag-zero captured DCB -> zero-control Type-3 frames -> SET_SH_REG Graphics IR -> fresh shader-register state -> typed pixel program GPU address -> duplicate-safe unique created-shader selection, with final PGM_LO/PGM_HI DCB word provenance and no fake SubmitDcb success or Vulkan work.
- The first resource-backed V3 target is selected (#172): an Astraea-owned offscreen 4x4 uniform-color graphics workload with no depth/blend/MSAA/textures/presentation; the first missing dependency is generic submitted context-register transport/state (#173).
- Generic AMD SET_CONTEXT_REG (0x69) lowering plus separate persistent initialized-vs-zero ContextRegisterState is complete (#173), with exact RawPacket provenance, explicit control/range failures, Trace v0 semantics, and no PS5-specific register meanings.
- #175 W0 bounded WRITE_DATA memory profile is complete (#177): exact first-profile PM4 0x37 control, typed guest GpuVirtualAddress + ordered inline payload Graphics IR, exact RawPacket provenance, and Trace v0 semantics, with no resource lookup, memory mutation, or Vulkan dependency.
- #175 W1 guest GPU buffer address-space resolution is complete (#179): explicitly registered non-overlapping GPU-domain buffer regions, stable logical buffer IDs, checked half-open range lookup, exact byte offsets/counts, and W0 payload-range composition with no backing memory or Vulkan identity.
- #175 W2 Vulkan transfer/readback proof is complete (#181): real vkCmdUpdateBuffer execution over the W0/W1 resolved guest buffer, explicit transfer->host synchronization, non-coherent flush/invalidate handling, stable guest buffer identity, and mandatory full-buffer Lavapipe readback with surrounding-byte verification.
- Public five-gate CI remains the merge requirement:
  - Linux x64
  - Windows x64
  - macOS ARM64
  - Linux ASan + UBSan
  - Linux Clang fuzz smoke

## Current frontier

1. V1 guest execution is proven through #148/#149 and persistent created-shader identity/transactional publication is complete through #162-#165.
2. V2's first deterministic SPIR-V backend is complete through #150/#151.
3. V3's first actual host-GPU proof is complete through #152/#153.
4. #154/#155 capture a real SubmitDcb descriptor/stream.
5. #156/#157 frame that stream as generic AMD PM4 Type-3 packets.
6. #158/#159 lower SET_SH_REG to typed shader-register Graphics IR.
7. #160/#161 persist shader-register state and reconstruct the existing pixel program GPU address.
8. #162/#163 materialize validated created pixel shaders and provide duplicate-safe lookup by that address.
9. #164/#165 register those created shaders transactionally in the real `sceAgcCreateShader` HLE path.
10. #166/#167 reconcile durable V1/V2/V3 documentation and record the semantic-IR/compiler-IR boundary after the rapid proof-chain merges.
11. #169 composes the captured DCB/state path through final pixel-program address resolution and unique created-shader selection with exact PGM source provenance.
12. #172 selects the first resource-backed V3 workload: a tiny owned offscreen uniform-color graphics proof, deliberately excluding presentation.
13. #173 adds generic PM4 SET_CONTEXT_REG lowering plus a separate persistent initialized-vs-zero ContextRegisterState without assigning PS5 meanings.
14. #175 inserts a bounded WRITE_DATA guest-buffer micro-gate before further raster orchestration so Astraea can establish guest GPU address/resource resolution independently of shader linkage, draw, export, and render-target semantics.
15. #177 completes #175 W0: ordinary AMD PM4 WRITE_DATA (0x37) direct-memory profile -> typed guest-GPU memory-write Graphics IR, preserving destination/payload semantics and packet provenance without resolving or mutating memory.
16. #179 completes #175 W1: non-overlapping predeclared guest GPU buffer regions resolve checked W0 write ranges to stable guest buffer IDs plus byte offsets/counts without backing-memory mutation or backend identity.
17. #181 completes #175 W2: raw bounded WRITE_DATA -> typed W0 operation -> W1 guest-buffer resolution -> real queued Vulkan transfer -> deterministic full-buffer Lavapipe readback, without exposing Vulkan handles as guest identity.
18. #175 is therefore complete as a resource-substrate micro-gate. Planning now returns to #172's offscreen raster workload; the next raster dependency must be selected from the expanded verified state rather than assumed from the pre-W0 ordering.

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
- semantic Shader IR / interpreter oracle
- workload-driven compiler/value IR when required
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

Return to **#172's owned offscreen 4x4 raster workload** and perform a fresh
dependency analysis using the now-proven resource substrate:

- submitted pixel shader selection is connected;
- generic SET_SH_REG and SET_CONTEXT_REG state exist;
- guest GPU virtual addresses have a distinct typed domain;
- explicitly declared guest GPU buffers resolve to stable guest identities;
- a resolved guest buffer operation has reached real Vulkan and deterministic
  readback.

Do not simply resume the old dependency list by position. Re-evaluate the
shortest honest raster path and choose the **first missing dependency** required
for a deterministic offscreen uniform-color result.

Candidate dependency classes to compare include:

- owned vertex-stage shader creation/identity;
- shader linkage / primitive state;
- minimal draw packet semantics;
- minimal vertex input/resource requirements;
- vertex position export semantics;
- pixel color export semantics;
- the narrow PS5 context-register meanings needed for one color target;
- guest color-target identity/layout;
- Vulkan graphics pipeline/image realization.

Prefer a workload shape that removes whole dependency classes rather than
implementing broad register/opcode coverage. Keep presentation/VideoOut out of
V3.

Before starting code, record the selected next dependency and its evidence in a
bounded issue/update to the #172 research plan.

Separately, the two independently proven headless Vulkan executors now justify
a future small refactor of their common loader/device/memory/submit substrate,
but that refactor must not displace the V3 critical path or alter either guest
semantic contract.

The PM4 Type-3 stream and bounded RDNA2 stream fuzz-smoke hardening follow-up
also remains valid when it can proceed without displacing the V3 critical path.
