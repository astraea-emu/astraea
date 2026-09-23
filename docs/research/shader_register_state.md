# Persistent shader-register state

## Purpose

Issue #160 adds the smallest persistent graphics state required after
PM4 SET_SH_REG lowering (#158/#159).

The state layer is intentionally narrower than a GPU register model. It stores
only the supported generic relative shader-register window and one
evidence-scoped pixel program-address interpretation.

## Generic state contract

AMD/Linux evidence used by #158 establishes SET_SH_REG as a consecutive write
into the relative shader-register window `[0, 0x400)`.

Astraea therefore stores exactly 0x400 uint32 values plus an independent
initialized bit for every register.

Initialization is semantic state. A register that has never been written is
not equivalent to a register explicitly written with value zero.

Applying a `GraphicsIrShaderRegisterWriteRange`:

- validates the complete range before mutation;
- writes values in source order;
- marks every destination initialized;
- preserves unrelated registers;
- permits deterministic later overwrite;
- reports only the applied start/count.

Unsupported Graphics IR fails without mutating state.

## Evidence-scoped pixel program profile

The V1 shader-object preparation work (#145/#147) established the first
supported pixel program-register pair:

- relative shader register 0x08: pixel PGM_LO;
- relative shader register 0x09: pixel PGM_HI.

For that profile, the prepared code GPU address is encoded as:

- PGM_LO = address bits 8..39;
- PGM_HI low byte = address bits 40..47.

The resolver therefore requires both registers to have been explicitly
initialized, rejects non-zero PGM_HI upper 24 bits, and reconstructs:

`gpu_va = (uint64_t(PGM_LO) << 8) | (uint64_t(PGM_HI & 0xff) << 40)`

The result is a dedicated GPU-domain address value. It is not a host pointer,
CPU guest address, shader registry handle, or Vulkan object.

## Scope boundary

This slice does not:

- infer other shader stages;
- interpret user-data/resource registers;
- apply context/uconfig registers;
- decode descriptors;
- process draw/index/event packets;
- look up created shaders;
- dereference the resolved GPU address;
- resume SubmitDcb;
- call Vulkan;
- add RDNA2 instructions.

The next dependency is a bounded created-shader registry/lookup bridge that
matches the resolved pixel program GPU address to the already-validated
sceAgcCreateShader payload.
