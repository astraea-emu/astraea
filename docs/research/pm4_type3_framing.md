# Generic PM4 Type-3 framing for captured AGC DCB streams

## Purpose

Issue #156 establishes packet extents over the exact raw DCB stream captured by
#154/#155.

This is framing only. No opcode, register, resource, synchronization, or draw
semantics are assigned.

## Pinned evidence

Checked 2026-09-22.

### AMD Linux/KFD

`torvalds/linux@fe2ec83746e501645709761605c2464a44fd2929`

`drivers/gpu/drm/amd/amdkfd/kfd_pm4_headers.h`

The public Type-3 header definition places packet type in bits 31:30, a 14-bit
count in bits 29:16, and opcode in bits 15:8. Count is the number of
information-body DWORDs minus one. Thus:

- body words = encoded count + 1;
- total words = encoded count + 2.

### GPUOpen PAL

`GPUOpen-Drivers/pal@c5e800072a32f68b6ccc4422936d96167c6e0728`

`src/core/hw/gfxip/gfx9/gfx9CmdUtil.cpp`

PAL independently constructs Type-3 headers with
`header.count = total_packet_dwords - 2`.

PAL also exposes control fields in the low header bits for some forms. Astraea
therefore preserves bits 7:0 as opaque `low_control_bits`; it does not repeat
the stale branch's inaccurate `reserved_low` label.

### PS5/AGC corroboration

`mattias800/prosper@a076095b311c263bde0aacba09fad8f3f04b5023`

Current public PS5 compatibility research models submitted AGC DCB streams as
PM4 input and routes `sceAgcDriverSubmitDcb` into a PM4 command processor.
This supports the frontend ordering only; individual PS5 opcode/register
semantics are not copied or inferred.

## Astraea boundary

`frame_pm4_type3_stream()`:

- requires whole-word input;
- accepts an empty stream;
- accepts Type-3 headers only in the first profile;
- extracts count/opcode structurally;
- preserves low header control bits opaquely;
- derives and bounds exact packet extents;
- preserves exact bytes through existing `RawPacket` provenance;
- assigns no opcode kind or behavior.

The stale `feat/m3-graphics-pm4-frontend` branch is historical reference only
and is intentionally not rebased or merged.
