#pragma once

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

struct VulkanGuestImageQueueCandidate {
    bool supports_graphics = false;
    std::uint32_t queue_count = 0;

    auto operator<=>(const VulkanGuestImageQueueCandidate&) const =
        default;
};

struct VulkanGuestImageMemoryCandidate {
    bool allowed = false;
    bool host_visible = false;
    bool host_coherent = false;
    bool device_local = false;

    auto operator<=>(const VulkanGuestImageMemoryCandidate&) const =
        default;
};

struct VulkanGuestImageFormatCandidate {
    bool color_attachment = false;
    bool transfer_source = false;
    bool transfer_destination = false;

    auto operator<=>(const VulkanGuestImageFormatCandidate&) const =
        default;
};

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_guest_image_queue_family(
    std::span<const VulkanGuestImageQueueCandidate>
        candidates) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_guest_image_device_memory_type(
    std::span<const VulkanGuestImageMemoryCandidate>
        candidates) noexcept;

[[nodiscard]] std::optional<std::uint32_t>
select_vulkan_guest_image_readback_memory_type(
    std::span<const VulkanGuestImageMemoryCandidate>
        candidates) noexcept;

[[nodiscard]] bool
supports_vulkan_guest_image_format(
    const VulkanGuestImageFormatCandidate&
        candidate) noexcept;

struct VulkanGuestImageReadbackPlan {
    GuestGpuImageId image_id;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t logical_byte_count = 0;

    auto operator<=>(const VulkanGuestImageReadbackPlan&) const =
        default;
};

enum class VulkanGuestImageReadbackErrorCode {
    invalid_image_descriptor,
    unsupported_guest_format,
    unsupported_surface_layout,
    surface_layout_mismatch,
    logical_size_overflow,
    logical_size_mismatch,
    logical_size_unrepresentable,
    loader_unavailable,
    instance_creation_failure,
    physical_device_enumeration_failure,
    no_compatible_physical_device,
    device_extension_enumeration_failure,
    device_creation_failure,
    image_creation_failure,
    no_image_memory_type,
    image_memory_allocation_failure,
    image_memory_bind_failure,
    staging_buffer_creation_failure,
    no_host_visible_memory_type,
    staging_memory_allocation_failure,
    staging_memory_bind_failure,
    staging_memory_map_failure,
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

struct VulkanGuestImageReadbackError {
    VulkanGuestImageReadbackErrorCode code =
        VulkanGuestImageReadbackErrorCode::
            invalid_image_descriptor;
    std::int32_t native_result = 0;

    auto operator<=>(const VulkanGuestImageReadbackError&) const =
        default;
};

using VulkanGuestImageReadbackPlanResult =
    astraea::core::Result<
        VulkanGuestImageReadbackPlan,
        VulkanGuestImageReadbackError>;

[[nodiscard]] VulkanGuestImageReadbackPlanResult
plan_vulkan_guest_image_readback(
    const GuestGpuImageView& image) noexcept;

struct VulkanGuestImageReadbackDeviceInfo {
    std::string name;
    std::uint32_t api_version = 0;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t device_type = 0;

    auto operator<=>(const VulkanGuestImageReadbackDeviceInfo&) const =
        default;
};

struct VulkanGuestImageReadbackExecution {
    GuestGpuImageId image_id;
    std::vector<std::byte> logical_pixels;
    VulkanGuestImageReadbackDeviceInfo device;

    auto operator<=>(const VulkanGuestImageReadbackExecution&) const =
        default;
};

using VulkanGuestImageReadbackExecutionResult =
    astraea::core::Result<
        VulkanGuestImageReadbackExecution,
        VulkanGuestImageReadbackError>;

// Materializes one already-typed supported guest image as a separate Vulkan
// image, clears it to opaque magenta, copies its logical pixels to a staging
// buffer, and returns those pixels.
//
// Guest allocation/image identity and GFX10 surface layout remain guest-domain
// state. This proof never aliases Vulkan memory to the guest allocation,
// mutates guest backing, or treats Vulkan row pitch as guest surface layout.
[[nodiscard]] VulkanGuestImageReadbackExecutionResult
execute_vulkan_guest_image_clear_readback(
    const GuestGpuImageView& image);

}  // namespace astraea::graphics
