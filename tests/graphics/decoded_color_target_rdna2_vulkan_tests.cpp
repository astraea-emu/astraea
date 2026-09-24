#include <astraea/graphics/color_target_state.hpp>
#include <astraea/graphics/gfx10_color_target_image.hpp>
#include <astraea/graphics/gpu_image.hpp>
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

void set_register(
    astraea::graphics::ContextRegisterState& state,
    std::uint16_t offset,
    std::uint32_t value) {
    state.values[offset] = value;
    state.initialized.set(offset);
}

astraea::graphics::ContextRegisterState
owned_color_target_state() {
    constexpr std::uint64_t kBase = 0x1200U;

    astraea::graphics::ContextRegisterState state{};
    set_register(
        state,
        astraea::graphics::kColorTargetMaskContextOffset,
        0x0fU);
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseContextOffset,
        static_cast<std::uint32_t>(
            (kBase >> 8U) & 0xffffffffULL));
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseExtContextOffset,
        static_cast<std::uint32_t>(
            kBase >> 40U));
    set_register(
        state,
        astraea::graphics::kColorTarget0InfoContextOffset,
        astraea::graphics::kGfx10ColorFormatR8G8B8A8 << 2U);
    set_register(
        state,
        astraea::graphics::kColorTarget0Attrib2ContextOffset,
        (3U << 14U) | 3U);
    set_register(
        state,
        astraea::graphics::kColorTarget0Attrib3ContextOffset,
        static_cast<std::uint32_t>(
            astraea::graphics::kGfx10ResourceType2d)
            << 24U);

    return state;
}

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

[[nodiscard]] bool live_vulkan_required() {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t value_size = 0U;
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
    "decoded ColorTarget0 and owned RDNA2 drive exact Vulkan pixels",
    "[graphics][v3][color-target][rdna2][vulkan][live]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan raster proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    const auto decoded =
        astraea::graphics::
            resolve_color_target0_context_state(
                owned_color_target_state());
    REQUIRE(decoded.has_value());

    const auto descriptor =
        astraea::graphics::
            plan_gfx10_linear_rgba8_unorm_color_target_image(
                decoded.value());
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->logical_byte_count == 64U);
    REQUIRE(
        descriptor->layout.surface_byte_count ==
        1024U);

    astraea::graphics::GuestGpuAllocationAddressSpace
        allocations;
    const auto allocation =
        allocations.register_allocation(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x1000U},
            0x2000U);
    REQUIRE(allocation.has_value());

    astraea::graphics::GuestGpuImageRegistry images;
    const auto image_id =
        images.register_view(
            descriptor.value(),
            allocations);
    REQUIRE(image_id.has_value());

    const auto* image =
        images.entry_at(image_id.value());
    REQUIRE(image != nullptr);
    REQUIRE(
        image->allocation_id ==
        allocation.value());
    REQUIRE(image->allocation_byte_offset == 0x200U);

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
                *image,
                vertex->words,
                fragment->words);

    REQUIRE(executed.has_value());
    REQUIRE(
        executed->image_id ==
        image_id.value());
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

TEST_CASE(
    "decoded ColorTarget0 rejects logical-only guest backing before Vulkan",
    "[graphics][v3][color-target][registration][negative]") {
    const auto decoded =
        astraea::graphics::
            resolve_color_target0_context_state(
                owned_color_target_state());
    REQUIRE(decoded.has_value());

    const auto descriptor =
        astraea::graphics::
            plan_gfx10_linear_rgba8_unorm_color_target_image(
                decoded.value());
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->logical_byte_count == 64U);
    REQUIRE(
        descriptor->layout.surface_byte_count ==
        1024U);

    astraea::graphics::GuestGpuAllocationAddressSpace
        allocations;
    REQUIRE(
        allocations.register_allocation(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x1200U},
            descriptor->logical_byte_count)
            .has_value());

    astraea::graphics::GuestGpuImageRegistry images;
    const auto image =
        images.register_view(
            descriptor.value(),
            allocations);

    REQUIRE_FALSE(image.has_value());
    REQUIRE(
        image.error().code ==
        astraea::graphics::
            GuestGpuImageRegistrationErrorCode::
                allocation_resolution_failure);
    REQUIRE(
        image.error().
            allocation_resolution_error.has_value());
    REQUIRE(
        image.error().
            allocation_resolution_error->code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                partially_mapped_range);
    REQUIRE(images.size() == 0U);
}
