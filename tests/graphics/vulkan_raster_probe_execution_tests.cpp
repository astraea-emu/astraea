#include <astraea/graphics/vulkan_raster_probe_execution.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::GuestGpuImageView image_view(
    std::uint32_t width = 4U,
    std::uint32_t height = 4U,
    std::uint64_t logical_bytes = 64U) {
    return astraea::graphics::GuestGpuImageView{
        .id =
            astraea::graphics::GuestGpuImageId{
                .value = 9U,
            },
        .allocation_id =
            astraea::graphics::GuestGpuAllocationId{
                .value = 4U,
            },
        .allocation_byte_offset = 0U,
        .descriptor =
            astraea::graphics::GuestGpuImageDescriptor{
                .base_address =
                    astraea::graphics::GpuVirtualAddress{
                        .value = 0x5000U,
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
    "Vulkan raster probe preserves image-plan rejection",
    "[graphics][vulkan][raster-probe][negative]") {
    const auto result =
        astraea::graphics::
            execute_vulkan_fullscreen_triangle_probe(
                image_view(
                    4U,
                    4U,
                    63U));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            VulkanRasterProbeErrorCode::
                image_plan_failure);
    REQUIRE(result.error().image_plan_error.has_value());
    REQUIRE(
        result.error().image_plan_error->code ==
        astraea::graphics::
            VulkanOffscreenImageErrorCode::
                logical_size_mismatch);
}

TEST_CASE(
    "Vulkan fullscreen triangle raster readback is deterministic",
    "[graphics][vulkan][raster-probe][live][oracle]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan raster proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    const auto executed =
        astraea::graphics::
            execute_vulkan_fullscreen_triangle_probe(
                image_view());

    REQUIRE(executed.has_value());
    REQUIRE(executed->image_id.value == 9U);
    REQUIRE(executed->vertex_count == 3U);
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
            std::byte{0xff});
        REQUIRE(
            executed->logical_pixels[base + 3U] ==
            std::byte{0xff});
    }
}
