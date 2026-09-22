# Headless Vulkan vector-probe execution

## Purpose

Issue #152 is Astraea's first actual host GPU execution gate.

It executes the V2 register-state compute probe only. The probe remains a
compiler/execution harness and is not a claim that PlayStation 5 VGPRs or AGC
resources are represented by Vulkan storage buffers.

## Public dependencies

Pinned on 2026-09-22:

- zeux/volk release `1.4.350`
- Khronos Vulkan-Headers tag `v1.4.350`
- Linux CI runtime: Ubuntu 24.04 Mesa Vulkan drivers / Vulkan loader

volk is used only as the public Vulkan runtime loader. Astraea does not require
a system Vulkan SDK at configure time.

## Execution contract

The executor:

1. validates the V2 probe layout and exact initial word count;
2. dynamically loads Vulkan;
3. creates a headless Vulkan 1.3 instance;
4. enables portability enumeration when exposed;
5. selects the first compute-capable physical-device queue;
6. creates one storage buffer and host-visible allocation;
7. uploads the exact V2 state buffer;
8. binds it at set 0 / binding 0;
9. creates the V2 compute shader/pipeline;
10. dispatches one workgroup;
11. records a compute-shader-write -> host-read memory barrier;
12. waits on a fence;
13. invalidates non-coherent memory when required;
14. returns the exact state-buffer words.

For non-coherent host-visible memory, Astraea flushes/invalidates the complete
mapped allocation with `VK_WHOLE_SIZE`. This is intentionally simpler and
less error-prone than introducing partial-range atom alignment before a real
workload requires partial mapped ranges.

## CI proof

Only Linux x64 is required to execute the Vulkan workload in this slice.
The CI job installs Mesa's Vulkan runtime and forces the Lavapipe software ICD,
then sets an explicit Astraea guard that makes absence of the Vulkan execution
proof a test failure.

Windows x64 and macOS ARM64 still compile the same backend through volk. Their
first V3 gate does not require a Vulkan runtime/device.

No surface or window-system integration is used.

## Scope boundary

This slice does not implement:

- swapchains or presentation;
- graphics pipelines;
- PS5 command-buffer decoding;
- AGC resource descriptors;
- real guest buffers/images/samplers;
- new Shader IR or RDNA2 operations.

After the differential probe succeeds, the next graphics task must be selected
from the smallest real PS5 frontend/resource dependency needed to replace the
synthetic register-state buffer.
