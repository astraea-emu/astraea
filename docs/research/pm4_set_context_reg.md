# Generic AMD SET_CONTEXT_REG evidence

**Accessed:** 2026-09-23  
**Issue:** #173  
**Scope:** generic PM4 context-register transport only.

This note establishes only the generic AMD packet envelope required for
Astraea's next V3 frontend slice. It does **not** assign PS5-specific meaning to
any context-register offset.

## Opcode and register window

Pinned Linux source:

- repository: `torvalds/linux`
- commit: `fe2ec83746e501645709761605c2464a44fd2929`
- file: `drivers/gpu/drm/amd/amdgpu/nvd.h`

The public AMDGPU definitions identify:

```text
PACKET3_SET_CONTEXT_REG       = 0x69
PACKET3_SET_CONTEXT_REG_START = 0x0000a000
PACKET3_SET_CONTEXT_REG_END   = 0x0000a400
```

For the packet's relative register index, that start/end pair gives a
0x400-dword context-register window.

The same public header keeps `SET_CONTEXT_REG_INDEX` as a distinct opcode.
Astraea therefore does not infer indexed/control semantics into the ordinary
0x69 profile.

## Packet body shape

Pinned Linux source:

- repository: `torvalds/linux`
- commit: `fe2ec83746e501645709761605c2464a44fd2929`
- file: `drivers/gpu/drm/amd/amdgpu/gfx_v11_0.c`

The AMDGPU context-state emission path publicly constructs the ordinary packet
as:

1. a Type-3 `PACKET3_SET_CONTEXT_REG` header using the register count;
2. one relative register index:
   `reg_index - PACKET3_SET_CONTEXT_REG_START`;
3. `reg_count` consecutive dword values from the state extent.

That is sufficient evidence for Astraea to represent the ordinary generic
packet as:

```text
relative start offset + consecutive dword values
```

within the 0x400-dword context window.

## Astraea support boundary

Issue #173 therefore supports only:

- Type-3 opcode 0x69;
- the already-framed packet envelope;
- a low-16-bit relative start offset;
- consecutive dword values;
- ranges wholly contained in 0x400 relative dwords;
- explicit initialized-vs-zero persistent state;
- exact raw-packet provenance.

The first profile requires the upper bits of the offset/control word to be
zero. Public evidence reviewed for this slice does not justify assigning those
bits any ordinary 0x69 semantics.

## Explicit non-claims

This evidence does **not** establish:

- PS5 meanings for context-register offsets;
- render-target, viewport, scissor, blend, or depth field layouts;
- `SET_CONTEXT_REG_INDEX` semantics;
- `SET_CONTEXT_REG_INDIRECT` semantics;
- queue/reset/context-roll behavior;
- resource addresses or surface layouts;
- draw semantics;
- Vulkan translation.

Those remain workload-driven evidence questions under #172.

## Clean-room rule

Linux AMDGPU is used as public generic AMD packet evidence only. Astraea's
types, lowering, state model, tests, and synthetic fixtures are independently
written and do not copy proprietary Sony material or third-party emulator
implementation code.
