# sceAgcDriverSubmitDcb capture boundary

## Purpose

Issue #154 begins the first real PS5 graphics-frontend input after the V3
host-GPU proof.

This slice captures the submitted DCB stream only. It deliberately does not
pretend the submission has executed and does not infer PM4 framing or packet
semantics.

## Public evidence

Re-checked on 2026-09-22.

Current public native PS5 code in `Rufidj/ps5link-sdk` declares:

`int sceAgcDriverSubmitDcb(void *submitDescription)`

and constructs the submit description with this source shape:

- pointer to command words;
- 32-bit word count;
- one-byte flag.

Current SharpProspero independently documents a 16-byte sequential object with
the same fields at offsets 0x00, 0x08, and 0x0c. Bytes 0x0d..0x0f are not
assigned semantics here and remain opaque.

The first supported capture profile caps word count at `0xFFFFF`, matching
the current public wrapper's documented twenty-bit submission bound. This is a
bounded compatibility profile, not a claim about every firmware/driver
revision.

Independent emulator/research implementations corroborate:

- `sceAgcDriverSubmitDcb`
- NID `UglJIZjGssM`
- `libSceAgcDriver`

As with the shader-create import, long-form `#lib#mod` tokens are local
import identifiers and are not globally fixed library names.

## Astraea contract

`plan_sce_agc_driver_submit_dcb()`:

- requires the Astraea-internal service ID;
- preserves the guest submit-description address;
- reads exactly 16 descriptor bytes;
- decodes only pointer/count/flag;
- preserves descriptor padding opaquely;
- copies exactly `word_count * 4` command bytes;
- permits a zero-count stream without dereferencing the words pointer;
- never writes guest memory;
- never decodes a PM4 header;
- never invokes Vulkan;
- never returns success to guest execution.

Astraea's existing `RawPacket` API is intentionally not used for the whole
submission. Packet framing still requires evidence for each packet extent.
The DCB capture is a raw word stream and remains so until the next
evidence-driven slice establishes the minimum framing boundary.
