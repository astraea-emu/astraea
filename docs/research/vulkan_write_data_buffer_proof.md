# Vulkan WRITE_DATA buffer execution proof

**Accessed:** 2026-09-23  
**Issues:** #175, #181  
**Scope:** W2 backend execution/readback for one already-typed and already-resolved WRITE_DATA memory operation.

## Purpose

W0 proves the guest PM4 packet semantics.

W1 proves guest GPU buffer identity and checked address-range resolution.

W2 proves that the same typed/resolved operation can be materialized through a
real Vulkan command and return deterministic full-buffer bytes.

The backend path is intentionally:

```text
GuestGpuBufferId
+ GuestGpuBufferResolution
+ GraphicsIrGpuMemoryWrite
    -> VkBuffer backend materialization
    -> vkCmdUpdateBuffer
    -> transfer-write -> host-read dependency
    -> queue submit + fence wait
    -> mapped-memory readback
```

No shader, descriptor, render pass, image, surface, or presentation behavior is
in this proof.

## Current Vulkan constraints

Current Khronos Vulkan documentation for `vkCmdUpdateBuffer` establishes:

- `dstBuffer` requires `VK_BUFFER_USAGE_TRANSFER_DST_BIT`;
- `dstOffset` must be a multiple of four bytes;
- `dataSize` must be non-zero, a multiple of four bytes, and no greater than
  65,536 bytes;
- source data is copied into command-buffer storage when
  `vkCmdUpdateBuffer` is recorded;
- the command is a transfer operation;
- command pools whose queue family supports transfer, graphics, or compute may
  record/execute the command.

References:

- https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdUpdateBuffer.html
- https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html

Astraea W0's largest possible first-profile payload is 16,381 dwords =
65,524 bytes because the PM4 Type-3 encoded-count field is already bounded by
the generic framer. That remains below Vulkan's single-update limit.

## Host-visible memory / synchronization

W2 allocates host-visible memory for deterministic readback.

The logical guest buffer bytes are zero-initialized through the host mapping
before submission. For a non-coherent memory type, W2 flushes that mapped
memory before device work.

After `vkCmdUpdateBuffer`, W2 records a transfer-write -> host-read dependency,
submits the command buffer, waits for the fence, and invalidates non-coherent
mapped memory before reading.

Current Khronos memory documentation states that non-coherent device writes
must be made visible to host operations through the appropriate device memory
dependency, host synchronization such as a fence wait, and
`vkInvalidateMappedMemoryRanges`.

Reference:

- https://docs.vulkan.org/spec/latest/chapters/memory.html

## Guest/backend identity boundary

The guest identity remains `GuestGpuBufferId`.

W2 does not expose any of these as guest identity:

- `VkBuffer`;
- `VkDeviceMemory`;
- `VkDeviceAddress`;
- mapped host pointer;
- queue/command-buffer/fence handles.

Those are temporary backend artifacts.

The W2 result returns the stable guest buffer ID, full logical buffer bytes,
and diagnostic device metadata only.

## Validation boundary

Before loading Vulkan, W2 validates:

- non-empty valid guest region;
- W1 buffer ID matches the region;
- non-empty in-bounds W1 range;
- W0 payload is non-empty and byte-size matched to W1;
- reconstructed guest destination equals region base + W1 byte offset;
- dword alignment;
- update-size ceiling;
- host/Vulkan buffer-size representation.

Invalid guest-domain input therefore cannot be converted into a backend command
and later misreported as a Vulkan failure.

## Mandatory live oracle

Linux CI already sets:

`ASTRAEA_REQUIRE_VULKAN_PROBE=1`

and forces Mesa Lavapipe through `VK_DRIVER_FILES`.

The W2 live test uses that same deterministic gate and exercises the actual
chain:

```text
raw PM4 WRITE_DATA bytes
    -> W0 lowerer
    -> W1 buffer registration + range resolution
    -> W2 vkCmdUpdateBuffer
    -> full-buffer readback
```

It writes four known dwords at a non-zero offset and requires all bytes outside
the resolved range to remain zero.

## Explicit non-claims

This proof does not:

- implement persistent Vulkan guest-resource lifetime;
- model PS5 page tables or GPU-VA allocation policy;
- claim PS5 cache behavior beyond the bounded W0 command profile;
- add descriptors, images, render targets, tiling, or shaders;
- execute arbitrary guest command buffers;
- make SubmitDcb guest-visible success possible;
- complete V3.

After W2, #175's resource-substrate micro-gate is complete and planning returns
to #172's offscreen raster workload.

## Refactoring note

W2 is Astraea's second headless Vulkan executor. It intentionally reuses the V2
vector probe's proven architectural patterns without forcing a broad backend
rewrite in the same semantic proof.

Once both executors are independently green, a later bounded refactor may
extract their proven common headless Vulkan bootstrap/memory/submit substrate
without changing either guest contract.
