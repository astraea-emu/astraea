#pragma once

#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_buffer_address_space.hpp>
#include <astraea/graphics/graphics_ir.hpp>

namespace astraea::graphics {

inline constexpr std::uint64_t
    kVulkanGpuBufferWriteMaxUpdateBytes = 65536U;

struct VulkanGpuBufferWriteQueueCandidate {
    bool supports_update_buffer = false;
    std::uint32_t queue_count = 0;

    auto operator<=>(
        const VulkanGpuBufferWriteQueueCandidate&) const = default;
};

struct VulkanGpuBufferWriteMemoryCandidate {
    bool allowed = false;
    bool host_visible = false;
    bool host_coherent = false;

    auto operator<=>(
        const VulkanGpuBufferWriteMemoryCandidate&) const = default;
};

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_gpu_buffer_write_queue_family(
    std::span<const VulkanGpuBufferWriteQueueCandidate>
        candidates) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_gpu_buffer_write_memory_type(
    std::span<const VulkanGpuBufferWriteMemoryCandidate>
        candidates) noexcept;

struct VulkanGpuBufferWritePlan {
    GuestGpuBufferId buffer_id;
    std::uint64_t buffer_byte_size = 0;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_count = 0;

    auto operator<=>(const VulkanGpuBufferWritePlan&) const = default;
};

enum class VulkanGpuBufferWriteErrorCode {
    invalid_region,
    buffer_id_mismatch,
    zero_sized_resolution,
    resolution_out_of_bounds,
    empty_payload,
    payload_size_overflow,
    payload_size_mismatch,
    destination_mismatch,
    unaligned_write,
    payload_too_large,
    loader_unavailable,
    instance_creation_failure,
    physical_device_enumeration_failure,
    no_physical_device,
    no_update_buffer_queue_family,
    device_extension_enumeration_failure,
    device_creation_failure,
    buffer_creation_failure,
    no_host_visible_memory_type,
    memory_allocation_failure,
    memory_bind_failure,
    memory_map_failure,
    memory_flush_failure,
    command_pool_creation_failure,
    command_buffer_allocation_failure,
    command_buffer_begin_failure,
    command_buffer_end_failure,
    fence_creation_failure,
    queue_submit_failure,
    fence_wait_failure,
    memory_invalidate_failure,
    host_allocation_failure,
};

struct VulkanGpuBufferWriteError {
    VulkanGpuBufferWriteErrorCode code =
        VulkanGpuBufferWriteErrorCode::invalid_region;
    std::int32_t native_result = 0;

    auto operator<=>(const VulkanGpuBufferWriteError&) const = default;
};

using VulkanGpuBufferWritePlanResult =
    astraea::core::Result<
        VulkanGpuBufferWritePlan,
        VulkanGpuBufferWriteError>;

[[nodiscard]] VulkanGpuBufferWritePlanResult
plan_vulkan_gpu_buffer_write(
    const GuestGpuBufferRegion& region,
    const GuestGpuBufferResolution& resolution,
    const GraphicsIrGpuMemoryWrite& operation) noexcept;

struct VulkanGpuBufferWriteDeviceInfo {
    std::string name;
    std::uint32_t api_version = 0;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t device_type = 0;

    auto operator<=>(const VulkanGpuBufferWriteDeviceInfo&) const =
        default;
};

struct VulkanGpuBufferWriteExecution {
    GuestGpuBufferId buffer_id;
    std::vector<std::byte> final_bytes;
    VulkanGpuBufferWriteDeviceInfo device;

    auto operator<=>(const VulkanGpuBufferWriteExecution&) const =
        default;
};

using VulkanGpuBufferWriteExecutionResult =
    astraea::core::Result<
        VulkanGpuBufferWriteExecution,
        VulkanGpuBufferWriteError>;

// Executes one already-typed and already-resolved W0/W1 guest GPU buffer write
// through a real Vulkan transfer command.
//
// This is a bounded proof API. It does not mutate W0/W1 guest state, persist a
// Vulkan resource mapping, execute arbitrary guest command buffers, or return
// guest-visible SubmitDcb success.
[[nodiscard]] VulkanGpuBufferWriteExecutionResult
execute_gpu_buffer_write_on_vulkan(
    const GuestGpuBufferRegion& region,
    const GuestGpuBufferResolution& resolution,
    const GraphicsIrGpuMemoryWrite& operation);

}  // namespace astraea::graphics
