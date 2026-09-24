# ADR 0009: Separate guest GPU allocation, image identity, and surface layout

**Status:** Accepted  
**Date:** 2026-09-24

## Context

Astraea already models guest GPU virtual addresses and bounded buffer-region
identity independently from Vulkan. The first V3 raster workload now needs a
render target.

A render target has at least three different sizes/identities that must not be
collapsed:

1. logical image extent and pixel content;
2. physical guest GPU surface layout/backing extent;
3. host-backend materialization such as a Vulkan image/buffer.

For the selected 4x4 RGBA8 research target, the logical pixel payload is
64 bytes. Public AMD GFX10 Addrlib behavior shows that ordinary aligned-linear
surfaces can require a larger physical pitch/backing extent. For a 32-bpp,
4-pixel-wide ordinary linear GFX10 surface, the documented generic calculation
uses a pitch alignment of `256 / 4 = 64` pixels, yielding a 256-byte row pitch
and a 1024-byte four-row slice. `LINEAR_GENERAL` is a separate swizzle mode
with different alignment behavior.

Treating the logical 64-byte payload as the guest allocation size would bake a
false surface-layout assumption into resource resolution and Vulkan
materialization before tiled surfaces are introduced.

## Decision

Astraea will model guest GPU storage, image interpretation, surface layout, and
host materialization as separate layers.

### GuestGpuAllocation

A guest GPU allocation owns stable guest-domain storage identity:

- stable allocation ID;
- guest GPU virtual-address range;
- byte extent;
- lifetime/ownership metadata;
- alias relationships when known.

It contains no Vulkan handle.

### Buffer and image views

Typed views interpret a range of a guest allocation.

A buffer view carries byte-oriented bounds/usage.

An image view carries guest-domain image semantics such as:

- format;
- logical width/height/depth;
- mip/layer information as required;
- sample count when required;
- surface-layout descriptor;
- optional compression/metadata identities when supported.

Multiple views may alias the same allocation.

### Surface layout

Surface layout is explicit guest-domain state, not inferred from a Vulkan
image.

For each supported profile it records the minimum information required to map
logical coordinates to guest backing storage, including as needed:

- swizzle/layout mode;
- pitch in elements and bytes;
- aligned height/depth;
- base alignment;
- slice/backing extent;
- block dimensions;
- metadata requirements such as DCC/CMASK/FMASK when supported.

Layout arithmetic must come from documented generic AMD behavior or controlled
PS5 observation. Unsupported modes fail explicitly.

### First V3 raster target

The first bounded target remains a 4x4, single-sample, one-color-attachment,
RGBA8 UNORM-style workload with DCC disabled.

The plan distinguishes:

- logical image content: 4x4x4 = 64 bytes;
- ordinary GFX10 aligned-linear surface backing: independently calculated
  pitch/alignment/extent;
- Vulkan target/readback representation: backend-specific and not guest
  identity.

The first raster proof may support only one exact surface-layout profile. It
must not generalize to tiled/compressed layouts until those mappings are
evidenced and tested.

### Backend boundary

The Vulkan backend materializes host resources from guest resource + layout
state. Guest GPU addresses must never be cast to host pointers, Vulkan handles,
or `VkDeviceAddress` values.

Readback/comparison operates through the guest surface-layout mapping so a
correct logical image is distinguished from a coincidentally matching
contiguous host buffer.

## Consequences

### Positive

- The first raster proof does not encode a false 64-byte physical-surface
  assumption.
- Buffer/image aliases can be represented above Vulkan.
- Tiled and compressed surfaces have a natural future home.
- Backend replacement does not redefine guest resource identity.
- Surface-layout bugs become testable independently from shader or Vulkan
  behavior.

### Negative / constraints

- The 4x4 proof requires more resource plumbing than treating the target as a
  flat byte array.
- Layout profiles remain intentionally narrow until evidence expands.
- A generic AMD layout implementation must not be mistaken for proof that every
  PS5 surface uses that profile.

## Alternatives considered

### Treat every image as a buffer

Rejected because it conflates storage with image interpretation and creates a
poor basis for tiling, compression, aliases, and host image realization.

### Let Vulkan define pitch/layout

Rejected because Vulkan's host layout is not the PS5 guest surface contract.

### Implement broad AMD Addrlib behavior immediately

Rejected as speculative breadth. Implement the smallest layout subset demanded
by the selected owned workload, with explicit unsupported cases.

## Evidence / references

- ADR 0006: guest resource identity exists above Vulkan.
- AMD/Mesa GFX10 Addrlib `HwlComputeSurfaceInfoLinear` public implementation.
- #175/#179/#181: guest GPU buffer identity and Vulkan-backed buffer proof.
- #172: selected deterministic offscreen graphics workload.
- Provisional #202/#203 research on Color Target 0 and the first raster target.

## Revisit triggers

Revisit if:

- controlled PS5 observation contradicts the selected generic AMD layout;
- a required guest image cannot be represented as an allocation plus typed
  view/layout;
- aliasing/lifetime behavior requires a stronger memory-object model; or
- a second graphics backend demonstrates that the boundary is unnecessarily
  Vulkan-shaped.
