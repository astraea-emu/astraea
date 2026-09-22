#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/spirv_vector_probe.hpp>

namespace astraea::graphics {

struct VulkanVectorProbeQueueCandidate {
    bool supports_compute = false;
    std::uint32_t queue_count = 0;

    auto operator<=>(const VulkanVectorProbeQueueCandidate&) const =
        default;
};

struct VulkanVectorProbeMemoryCandidate {
    bool allowed = false;
    bool host_visible = false;
    bool host_coherent = false;

    auto operator<=>(const VulkanVectorProbeMemoryCandidate&) const =
        default;
};

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_vector_probe_queue_family(
    std::span<const VulkanVectorProbeQueueCandidate>
        candidates) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_vector_probe_memory_type(
    std::span<const VulkanVectorProbeMemoryCandidate>
        candidates) noexcept;

struct VulkanVectorProbeDeviceInfo {
    std::string name;
    std::uint32_t api_version = 0;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t device_type = 0;

    auto operator<=>(const VulkanVectorProbeDeviceInfo&) const =
        default;
};

struct VulkanVectorProbeExecution {
    std::vector<std::uint32_t> final_words;
    VulkanVectorProbeDeviceInfo device;

    auto operator<=>(const VulkanVectorProbeExecution&) const =
        default;
};

enum class VulkanVectorProbeExecutionErrorCode {
    invalid_probe_layout,
    input_word_count_mismatch,
    size_overflow,
    loader_unavailable,
    instance_creation_failure,
    physical_device_enumeration_failure,
    no_physical_device,
    no_compute_queue_family,
    device_extension_enumeration_failure,
    device_creation_failure,
    buffer_creation_failure,
    no_host_visible_memory_type,
    memory_allocation_failure,
    memory_bind_failure,
    memory_map_failure,
    memory_flush_failure,
    descriptor_set_layout_creation_failure,
    descriptor_pool_creation_failure,
    descriptor_set_allocation_failure,
    shader_module_creation_failure,
    pipeline_layout_creation_failure,
    compute_pipeline_creation_failure,
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

struct VulkanVectorProbeExecutionError {
    VulkanVectorProbeExecutionErrorCode code =
        VulkanVectorProbeExecutionErrorCode::
            invalid_probe_layout;
    std::int32_t native_result = 0;

    auto operator<=>(const VulkanVectorProbeExecutionError&) const =
        default;
};

using VulkanVectorProbeExecutionResult =
    astraea::core::Result<
        VulkanVectorProbeExecution,
        VulkanVectorProbeExecutionError>;

// Executes exactly one V2 register-state compute probe headlessly.
//
// No surface, WSI, swapchain, graphics pipeline, guest AGC resource mapping,
// or presentation semantics are involved. The supplied word vector must match
// the exact V2 probe layout size.
[[nodiscard]] VulkanVectorProbeExecutionResult
execute_spirv_vector_probe_on_vulkan(
    const SpirvVectorProbeModule& module,
    std::span<const std::uint32_t> initial_words);

}  // namespace astraea::graphics
