# Generic AMD PM4 WRITE_DATA evidence

**Accessed:** 2026-09-23  
**Issues:** #175, #177  
**Scope:** first bounded WRITE_DATA direct-memory semantic profile.

This note records the public evidence used for Astraea's W0 command semantic
slice. W0 does not resolve or mutate guest GPU memory.

## Generic AMD packet definition

Pinned Linux source:

- repository: `torvalds/linux`
- commit: `fe2ec83746e501645709761605c2464a44fd2929`
- file: `drivers/gpu/drm/amd/amdgpu/soc15d.h`

The public AMDGPU definitions identify:

```text
PACKET3_WRITE_DATA = 0x37

WRITE_DATA_DST_SEL(x)        = x << 8
WR_ONE_ADDR                  = 1 << 16
WR_CONFIRM                   = 1 << 20
WRITE_DATA_CACHE_POLICY(x)   = x << 25
WRITE_DATA_ENGINE_SEL(x)     = x << 30
```

The same source defines:

- destination selector 5 as direct asynchronous memory;
- address-increment value 0 as incrementing the address;
- write-confirm value 1 as waiting for write confirmation;
- cache policy 0 as LRU;
- engine selector 0 as ME;
- the low direct-memory address field as dword aligned;
- a separate high address dword.

For W0, Astraea accepts only the exact control word:

```text
0x00100500
```

That is:

```text
dst_sel       = 5
addr_incr     = 0
write_confirm = 1
cache_policy  = 0
engine_sel    = 0
other bits    = 0
```

This is an evidence-bounded allowlist, not a claim that other WRITE_DATA modes
are invalid.

## Concrete public AMD memory-write sequence

Pinned Linux source:

- same repository/commit;
- file: `drivers/gpu/drm/amd/amdgpu/gfx_v11_0.c`.

A public AMDGPU test constructs:

```text
ib.ptr[0] = PACKET3(PACKET3_WRITE_DATA, 3);
ib.ptr[1] = WRITE_DATA_DST_SEL(5) | WR_CONFIRM;
ib.ptr[2] = lower_32_bits(gpu_addr);
ib.ptr[3] = upper_32_bits(gpu_addr);
ib.ptr[4] = 0xDEADBEEF;
```

This establishes the ordinary first-profile packet body:

```text
control
destination low
destination high
inline payload dwords...
```

Astraea's already-merged Type-3 framer defines:

```text
body_word_count  = encoded_count + 1
total_word_count = encoded_count + 2
```

Therefore, for WRITE_DATA:

```text
payload_count = encoded_count - 2
```

and one payload dword has `encoded_count = 3` and five total packet dwords.

## PS5-facing corroboration

Pinned public source:

- repository: `SvenGDK/SharpProspero`
- commit: `9220876e25bc28aca1f65ea644783a479949ad77`
- file: `src/SharpProspero/Interop/Agc/SceAgc.cs`.

Its PS5 AGC interop surface exposes:

`sceAgcDcbWriteData(commandBuffer, destSel, cachePolicy, dstAddr, srcData,
dwordCount, writeConfirm, flags)`

and describes the call as appending a WRITE_DATA packet that copies inline
dwords into a GPU memory address.

This corroborates that WRITE_DATA is relevant to the PS5 DCB API surface. It
does not independently establish every raw command-processor bit, so W0's raw
semantics remain grounded in generic AMD public packet definitions.

## Evidence explicitly excluded

Kyty currently exposes a `GraphicsDcbWriteData` HLE function, but its public
builder uses an emulator-private NOP-wrapped command rather than raw
`PACKET3_WRITE_DATA`. It is therefore not used as evidence for PS5 raw packet
encoding.

shadPS4 implements generic AMD WRITE_DATA behavior for PS4-era command streams.
It can corroborate the general AMD family behavior but is not evidence that a
PS5 AGC builder emits an identical raw stream.

## W0 semantic boundary

Astraea W0 supports exactly:

- Type-3 opcode `0x37`;
- Type-3 low control byte = 0;
- control word `0x00100500`;
- dword-aligned 64-bit GPU destination address reconstructed from low/high
  packet dwords;
- one or more inline payload dwords;
- exact RawPacket provenance.

The resulting Graphics IR contains a typed guest `GpuVirtualAddress` and
ordered dword payload.

W0 deliberately does not:

- validate PS5 GPU virtual-address width/canonicality;
- look up a guest allocation;
- cast the address to a CPU guest pointer or host pointer;
- associate the address with a Vulkan object or `VkDeviceAddress`;
- mutate memory;
- model cache completion beyond preserving the supported control profile;
- support register, GDS, TC-L2, alternate engine/cache, one-address, or
  write-confirm-off modes;
- make SubmitDcb guest-visible success possible.

Mapped-range/resource validity belongs to #175 W1.

## Clean-room rule

Linux AMDGPU supplies generic AMD packet evidence. SharpProspero supplies
public PS5-facing API corroboration only.

Astraea's types, lowerer, tests, and synthetic packet fixtures are
independently written. No proprietary Sony SDK material, firmware, keys,
retail assets, or third-party emulator implementation code is copied.
