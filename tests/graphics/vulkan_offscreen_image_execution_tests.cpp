#include <astraea/graphics/vulkan_offscreen_image_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

[[nodiscard]] astraea::graphics::GuestGpuImageView
image_view(
    std::uint32_t width = 4U,
    std::uint32_t height = 4U,
    std::uint64_t logical_bytes = 64U,
    std::uint64_t image_id = 7U) {
    return astraea::graphics::GuestGpuImageView{
        .id =
            astraea::graphics::GuestGpuImageId{
                .value = image_id,
            },
        .allocation_id =
            astraea::graphics::GuestGpuAllocationId{
                .value = 3U,
            },
        .allocation_byte_offset = 0U,
        .descriptor =
            astraea::graphics::GuestGpuImageDescriptor{
                .base_address =
                    astraea::graphics::GpuVirtualAddress{
                        .value = 0x4000U,
                    },
                .format =
                    astraea::graphics::
                        GuestGpuImageFormat::
                            r8g8b8a8_unorm,
                .width = width,
                .height = height,
                .bytes_per_pixel = 4U,
                .logical_byte_count = logical_bytes,
                .layout =
                    astraea::graphics::GuestGpuSurfaceLayout{
                        .kind =
                            astraea::graphics::
                                GuestGpuSurfaceLayoutKind::
                                    gfx10_aligned_linear,
                        .pitch_pixels = 64U,
                        .pitch_bytes = 256U,
                        .base_alignment_bytes = 256U,
                        .surface_byte_count = 1024U,
                    },
            },
    };
}

[[nodiscard]] bool live_vulkan_required() {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(
            &value,
            &value_size,
            "ASTRAEA_REQUIRE_VULKAN_PROBE") != 0 ||
        value == nullptr) {
        return false;
    }

    const bool required =
        std::string_view{value} == "1";
    std::free(value);
    return required;
#else
    const auto* value =
        std::getenv("ASTRAEA_REQUIRE_VULKAN_PROBE");
    return value != nullptr &&
           std::string_view{value} == "1";
#endif
}

}  // namespace

TEST_CASE(
    "Vulkan offscreen queue selection chooses first usable graphics family",
    "[graphics][vulkan][offscreen-image][selection]") {
    const std::array<
        astraea::graphics::VulkanOffscreenImageQueueCandidate,
        4>
        candidates{
            astraea::graphics::VulkanOffscreenImageQueueCandidate{
                .supports_graphics = false,
                .queue_count = 2U,
            },
            astraea::graphics::VulkanOffscreenImageQueueCandidate{
                .supports_graphics = true,
                .queue_count = 0U,
            },
            astraea::graphics::VulkanOffscreenImageQueueCandidate{
                .supports_graphics = true,
                .queue_count = 1U,
            },
            astraea::graphics::VulkanOffscreenImageQueueCandidate{
                .supports_graphics = true,
                .queue_count = 4U,
            },
        };

    REQUIRE(
        astraea::graphics::
            select_vulkan_offscreen_graphics_queue_family(
                candidates) ==
        std::optional<std::uint32_t>{2U});
}

TEST_CASE(
    "Vulkan offscreen device memory prefers device local",
    "[graphics][vulkan][offscreen-image][selection]") {
    const std::array<
        astraea::graphics::VulkanOffscreenImageMemoryCandidate,
        3>
        candidates{
            astraea::graphics::VulkanOffscreenImageMemoryCandidate{
                .allowed = true,
                .device_local = false,
                .host_visible = true,
                .host_coherent = true,
            },
            astraea::graphics::VulkanOffscreenImageMemoryCandidate{
                .allowed = false,
                .device_local = true,
                .host_visible = false,
                .host_coherent = false,
            },
            astraea::graphics::VulkanOffscreenImageMemoryCandidate{
                .allowed = true,
                .device_local = true,
                .host_visible = false,
                .host_coherent = false,
            },
        };

    REQUIRE(
        astraea::graphics::
            select_vulkan_offscreen_device_memory_type(
                candidates) ==
        std::optional<std::uint32_t>{2U});
}

TEST_CASE(
    "Vulkan offscreen readback memory prefers coherent host visible",
    "[graphics][vulkan][offscreen-image][selection]") {
    const std::array<
        astraea::graphics::VulkanOffscreenImageMemoryCandidate,
        4>
        candidates{
            astraea::graphics::VulkanOffscreenImageMemoryCandidate{
                .allowed = true,
                .device_local = true,
                .host_visible = false,
                .host_coherent = false,
            },
            astraea::graphics::VulkanOffscreenImageMemoryCandidate{
                .allowed = true,
                .device_local = false,
                .host_visible = true,
                .host_coherent = false,
            },
            astraea::graphics::VulkanOffscreenImageMemoryCandidate{
                .allowed = false,
                .device_local = false,
                .host_visible = true,
                .host_coherent = true,
            },
            astraea::graphics::VulkanOffscreenImageMemoryCandidate{
                .allowed = true,
                .device_local = false,
                .host_visible = true,
                .host_coherent = true,
            },
        };

    REQUIRE(
        astraea::graphics::
            select_vulkan_offscreen_readback_memory_type(
                candidates) ==
        std::optional<std::uint32_t>{3U});
}

TEST_CASE(
    "Vulkan offscreen planner preserves typed image identity and logical extent",
    "[graphics][vulkan][offscreen-image][plan]") {
    const auto planned =
        astraea::graphics::
            plan_vulkan_offscreen_image(
                image_view());

    REQUIRE(planned.has_value());
    REQUIRE(planned->image_id.value == 7U);
    REQUIRE(planned->width == 4U);
    REQUIRE(planned->height == 4U);
    REQUIRE(planned->logical_byte_count == 64U);
}

TEST_CASE(
    "Vulkan offscreen planner rejects malformed logical image state",
    "[graphics][vulkan][offscreen-image][plan][negative]") {
    SECTION("unsupported format") {
        auto image = image_view();
        image.descriptor.format =
            static_cast<
                astraea::graphics::GuestGpuImageFormat>(
                    99U);
        const auto result =
            astraea::graphics::
                plan_vulkan_offscreen_image(image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                VulkanOffscreenImageErrorCode::
                    unsupported_image_format);
    }

    SECTION("zero extent") {
        const auto result =
            astraea::graphics::
                plan_vulkan_offscreen_image(
                    image_view(0U, 4U, 0U));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                VulkanOffscreenImageErrorCode::
                    invalid_image_extent);
    }

    SECTION("bytes per pixel") {
        auto image = image_view();
        image.descriptor.bytes_per_pixel = 8U;
        const auto result =
            astraea::graphics::
                plan_vulkan_offscreen_image(image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                VulkanOffscreenImageErrorCode::
                    invalid_bytes_per_pixel);
    }

    SECTION("logical byte count") {
        const auto result =
            astraea::graphics::
                plan_vulkan_offscreen_image(
                    image_view(4U, 4U, 63U));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                VulkanOffscreenImageErrorCode::
                    logical_size_mismatch);
    }
}

TEST_CASE(
    "Vulkan offscreen image clear readback is deterministic",
    "[graphics][vulkan][offscreen-image][live][oracle]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    const auto executed =
        astraea::graphics::
            execute_vulkan_offscreen_image_clear(
                image_view(),
                std::array<std::uint8_t, 4>{
                    255U,
                    0U,
                    0U,
                    255U,
                });

    REQUIRE(executed.has_value());
    REQUIRE(executed->image_id.value == 7U);
    REQUIRE(executed->logical_pixels.size() == 64U);
    REQUIRE_FALSE(executed->device.name.empty());

    for (std::size_t pixel = 0U;
         pixel < 16U;
         ++pixel) {
        const auto base = pixel * 4U;
        REQUIRE(
            executed->logical_pixels[base + 0U] ==
            std::byte{0xff});
        REQUIRE(
            executed->logical_pixels[base + 1U] ==
            std::byte{0x00});
        REQUIRE(
            executed->logical_pixels[base + 2U] ==
            std::byte{0x00});
        REQUIRE(
            executed->logical_pixels[base + 3U] ==
            std::byte{0xff});
    }
}
