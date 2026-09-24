#include <astraea/graphics/shader_export_probe_compiler.hpp>
#include <astraea/graphics/shader_program.hpp>
#include <astraea/graphics/vulkan_raster_probe_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kExpBase = 0xf8000000U;
constexpr std::uint32_t kEndPgm = 0xbf810000U;

constexpr std::uint32_t make_exp_word0(
    std::uint8_t target) {
    return kExpBase |
           0x0fU |
           ((static_cast<std::uint32_t>(target) & 0x3fU) << 4U) |
           (1U << 11U);
}

constexpr std::uint32_t make_exp_sources() {
    return
        0U |
        (1U << 8U) |
        (2U << 16U) |
        (3U << 24U);
}

astraea::graphics::ShaderIrProgram
owned_export_program(std::uint8_t target) {
    const std::array<std::uint32_t, 3> words{
        make_exp_word0(target),
        make_exp_sources(),
        kEndPgm,
    };
    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());
    return program.value();
}

astraea::graphics::GuestGpuImageView image_view() {
    return astraea::graphics::GuestGpuImageView{
        .id =
            astraea::graphics::GuestGpuImageId{
                .value = 23U,
            },
        .allocation_id =
            astraea::graphics::GuestGpuAllocationId{
                .value = 11U,
            },
        .allocation_byte_offset = 0U,
        .descriptor =
            astraea::graphics::GuestGpuImageDescriptor{
                .base_address =
                    astraea::graphics::GpuVirtualAddress{
                        .value = 0x8000U,
                    },
                .format =
                    astraea::graphics::
                        GuestGpuImageFormat::
                            r8g8b8a8_unorm,
                .width = 4U,
                .height = 4U,
                .bytes_per_pixel = 4U,
                .logical_byte_count = 64U,
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
    "owned RDNA2 export probes rasterize through generated graphics SPIR-V",
    "[graphics][v3][rdna2][vulkan][raster][live]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan raster proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    const auto vertex_ir =
        astraea::graphics::
            compile_shader_export_probe(
                owned_export_program(0x0cU),
                astraea::graphics::
                    make_fullscreen_position_probe_launch_abi());
    const auto fragment_ir =
        astraea::graphics::
            compile_shader_export_probe(
                owned_export_program(0x00U),
                astraea::graphics::
                    make_magenta_fragment_probe_launch_abi());
    REQUIRE(vertex_ir.has_value());
    REQUIRE(fragment_ir.has_value());

    const auto vertex =
        astraea::graphics::
            lower_shader_export_probe_to_spirv(
                vertex_ir.value());
    const auto fragment =
        astraea::graphics::
            lower_shader_export_probe_to_spirv(
                fragment_ir.value());
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());

    const auto executed =
        astraea::graphics::
            execute_vulkan_fullscreen_triangle_spirv(
                image_view(),
                vertex->words,
                fragment->words);

    REQUIRE(executed.has_value());
    REQUIRE(executed->image_id.value == 23U);
    REQUIRE(executed->vertex_count == 3U);
    REQUIRE(executed->logical_pixels.size() == 64U);

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
