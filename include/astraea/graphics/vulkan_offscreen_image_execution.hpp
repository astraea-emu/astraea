#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_image.hpp>

namespace astraea::graphics {

struct VulkanOffscreenImageQueueCandidate {
    bool supports_graphics = false;
    std::uint32_t queue_count = 0;

    auto operator<=>(const VulkanOffscreenImageQueueCandidate&) const =
        default;
};

struct VulkanOffscreenImageMemoryCandidate {
    bool allowed = false;
    bool device_local = false;
    bool host_visible = false;
    bool host_coherent = false;

    auto operator<=>(const VulkanOffscreenImageMemoryCandidate&) const =
        default;
};

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_offscreen_graphics_queue_family(
    std::span<const VulkanOffscreenImageQueueCandidate>
        candidates) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_offscreen_device_memory_type(
    std::span<const VulkanOffscreenImageMemoryCandidate>
        candidates) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_offscreen_readback_memory_type(
    std::span<const VulkanOffscreenImageMemoryCandidate>
        candidates) noexcept;

struct VulkanOffscreenImagePlan {
    GuestGpuImageId image_id;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t logical_byte_count = 0;

    auto operator<=>(const VulkanOffscreenImagePlan&) const = default;
};

enum class VulkanOffscreenImageErrorCode {
    unsupported_image_format,
    invalid_image_extent,
    invalid_bytes_per_pixel,
    logical_size_overflow,
    logical_size_mismatch,
    logical_size_unrepresentable,
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

struct VulkanOffscreenImageError {
    VulkanOffscreenImageErrorCode code =
        VulkanOffscreenImageErrorCode::unsupported_image_format;
    std::int32_t native_result = 0;

    auto operator<=>(const VulkanOffscreenImageError&) const = default;
};

using VulkanOffscreenImagePlanResult =
    astraea::core::Result<
        VulkanOffscreenImagePlan,
        VulkanOffscreenImageError>;

[[nodiscard]] VulkanOffscreenImagePlanResult
plan_vulkan_offscreen_image(
    const GuestGpuImageView& image) noexcept;

struct VulkanOffscreenImageDeviceInfo {
    std::string name;
    std::uint32_t api_version = 0;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t device_type = 0;

    auto operator<=>(const VulkanOffscreenImageDeviceInfo&) const =
        default;
};

struct VulkanOffscreenImageExecution {
    GuestGpuImageId image_id;
    std::vector<std::byte> logical_pixels;
    VulkanOffscreenImageDeviceInfo device;

    auto operator<=>(const VulkanOffscreenImageExecution&) const =
        default;
};

using VulkanOffscreenImageExecutionResult =
    astraea::core::Result<
        VulkanOffscreenImageExecution,
        VulkanOffscreenImageError>;

// Materializes one typed guest image as a headless Vulkan
// VK_FORMAT_R8G8B8A8_UNORM image, clears it on-GPU, copies the logical image
// into a tightly packed host-visible staging buffer, and returns that readback.
//
// Guest physical pitch/tiling remains guest-domain state. This proof validates
// host image materialization/readback only; it does not write guest backing,
// create a graphics pipeline, execute a draw, or provide guest-visible success.
[[nodiscard]] VulkanOffscreenImageExecutionResult
execute_vulkan_offscreen_image_clear(
    const GuestGpuImageView& image,
    std::array<std::uint8_t, 4> rgba);

}  // namespace astraea::graphics
