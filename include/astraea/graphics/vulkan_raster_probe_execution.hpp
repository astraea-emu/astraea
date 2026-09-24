#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_image.hpp>
#include <astraea/graphics/vulkan_offscreen_image_execution.hpp>

namespace astraea::graphics {

enum class VulkanRasterProbeErrorCode {
    image_plan_failure,
    loader_unavailable,
    instance_creation_failure,
    physical_device_enumeration_failure,
    no_physical_device,
    no_compatible_graphics_device,
    device_extension_enumeration_failure,
    device_creation_failure,
    image_creation_failure,
    image_view_creation_failure,
    staging_buffer_creation_failure,
    no_device_memory_type,
    no_host_visible_memory_type,
    memory_allocation_failure,
    image_memory_bind_failure,
    staging_memory_bind_failure,
    memory_map_failure,
    shader_module_creation_failure,
    render_pass_creation_failure,
    framebuffer_creation_failure,
    pipeline_layout_creation_failure,
    graphics_pipeline_creation_failure,
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

struct VulkanRasterProbeError {
    VulkanRasterProbeErrorCode code =
        VulkanRasterProbeErrorCode::image_plan_failure;
    std::int32_t native_result = 0;
    std::optional<VulkanOffscreenImageError>
        image_plan_error;

    auto operator<=>(const VulkanRasterProbeError&) const = default;
};

struct VulkanRasterProbeExecution {
    GuestGpuImageId image_id;
    std::vector<std::byte> logical_pixels;
    VulkanOffscreenImageDeviceInfo device;
    std::uint32_t vertex_count = 0;

    auto operator<=>(const VulkanRasterProbeExecution&) const = default;
};

using VulkanRasterProbeExecutionResult =
    astraea::core::Result<
        VulkanRasterProbeExecution,
        VulkanRasterProbeError>;

// Materializes one typed RGBA8 guest image as a host Vulkan image, executes
// Astraea's owned fullscreen-triangle SPIR-V probe with exactly one
// vkCmdDraw(3, 1, 0, 0), copies logical pixels to host-visible staging memory,
// and returns deterministic readback.
//
// This is a host-backend oracle. It does not execute guest/AGC shaders, infer
// PS5 stage I/O, mutate guest physical surface backing, or present to a window.
[[nodiscard]] VulkanRasterProbeExecutionResult
execute_vulkan_fullscreen_triangle_probe(
    const GuestGpuImageView& image);

}  // namespace astraea::graphics
