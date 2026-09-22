# SPIR-V vector probe backend

## Scope

This note records the evidence and deliberately narrow contract for Astraea's
first Shader IR -> SPIR-V backend (#150).

The backend is a **register-state compute probe**. It exists so Astraea can
compile already-understood vector Shader IR, validate the generated module,
then execute that exact state transition on Vulkan in the next vertical gate.
It is not a claim that PlayStation 5 VGPRs are Vulkan storage-buffer resources.

## Pinned public toolchain

Observed and re-checked on 2026-09-22:

- Khronos SPIRV-Tools release: `v2026.3`
- release date: 2026-08-11
- matching SPIRV-Headers revision from that release's `DEPS`:
  `29981f65241605e08b0ede4cfeb999fe3b723c6a`
- validator environment: `SPV_ENV_VULKAN_1_3`

The public SPIRV-Tools API maps Vulkan 1.3 to SPIR-V 1.6. Astraea uses only
public `spvtools::SpirvTools` validation/disassembly APIs; it does not depend
on SPIRV-Tools internal IR or builder classes.

Production emission uses the public generated SPIRV-Headers enums and writes
the binary module directly.

## Probe ABI v0

The generated module is a compute shader with:

- one invocation per Astraea lane;
- fixed local size of 32 or 64 in X, 1 in Y/Z;
- `LocalInvocationIndex` as the lane index;
- descriptor set 0, binding 0;
- one `StorageBuffer` block containing a runtime array of 32-bit words;
- `ArrayStride = 4` and member offset 0;
- flattened register address:
  `vgpr_index * wave_size + lane`.

The lowering result records wave size, descriptor coordinates, VGPR stride,
required VGPR count, and required state-buffer word count so the Vulkan gate
does not have to reverse-engineer the SPIR-V module.

## Supported Shader IR

Only a straight-line program containing:

- `ShaderIrNop`;
- `ShaderIrVectorMove32`;
- `ShaderIrVectorAddF32`;
- exactly one terminal `ShaderIrEndProgram`.

All existing scalar, branch, wait, barrier, unsupported, and other operations
fail explicitly.

`V_MOV_B32` is a raw uint32 load/store.

`V_ADD_F32` loads uint32 source bits, bitcasts both to float32, performs one
`OpFAdd`, bitcasts the result to uint32, and stores it. This compiler slice
does not broaden Astraea's floating-point claims. The next Vulkan execution
proof must use only the existing interpreter's exact finite-normal,
mode-independent V_ADD cases first.

All lanes are active in probe ABI v0. EXEC-mask lowering is intentionally
deferred.

## Clean separation

This slice does not:

- call the Vulkan API;
- create devices, pipelines, or shader modules;
- interpret real AGC descriptors/resources;
- infer vertex/fragment IO;
- add generic RDNA2 instructions;
- lower CFG branches or scalar state;
- claim general PS5 shader compatibility.

The next dependency is one headless Vulkan compute execution of the same
probe-state buffer, compared bit-for-bit with Astraea's existing interpreter.
