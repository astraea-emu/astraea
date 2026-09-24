#include <astraea/graphics/gfx10_color_target_image.hpp>

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

void set_register(
    astraea::graphics::ContextRegisterState& state,
    std::uint16_t offset,
    std::uint32_t value) {
    state.values[offset] = value;
    state.initialized.set(offset);
}

astraea::graphics::ContextRegisterState
context_state(
    std::uint64_t address = 0x1200U,
    std::uint32_t width = 4U,
    std::uint32_t height = 4U,
    std::uint32_t info =
        astraea::graphics::kGfx10ColorFormatR8G8B8A8 << 2U,
    std::uint8_t swizzle =
        astraea::graphics::kGfx10ColorSwizzleLinear,
    std::uint8_t resource_type =
        astraea::graphics::kGfx10ResourceType2d,
    std::uint32_t target_mask = 0x0fU) {
    astraea::graphics::ContextRegisterState state{};

    set_register(
        state,
        astraea::graphics::kColorTargetMaskContextOffset,
        target_mask);
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseContextOffset,
        static_cast<std::uint32_t>(
            (address >> 8U) & 0xffffffffULL));
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseExtContextOffset,
        static_cast<std::uint32_t>(
            address >> 40U));
    set_register(
        state,
        astraea::graphics::kColorTarget0InfoContextOffset,
        info);
    set_register(
        state,
        astraea::graphics::kColorTarget0Attrib2ContextOffset,
        ((width - 1U) << 14U) |
            (height - 1U));
    set_register(
        state,
        astraea::graphics::kColorTarget0Attrib3ContextOffset,
        (static_cast<std::uint32_t>(swizzle) << 14U) |
            (static_cast<std::uint32_t>(
                 resource_type)
             << 24U));

    return state;
}

astraea::graphics::ColorTarget0ContextState
decoded_target(
    std::uint64_t address = 0x1200U,
    std::uint32_t width = 4U,
    std::uint32_t height = 4U,
    std::uint32_t info =
        astraea::graphics::kGfx10ColorFormatR8G8B8A8 << 2U,
    std::uint8_t swizzle =
        astraea::graphics::kGfx10ColorSwizzleLinear,
    std::uint8_t resource_type =
        astraea::graphics::kGfx10ResourceType2d,
    std::uint32_t target_mask = 0x0fU) {
    const auto decoded =
        astraea::graphics::
            resolve_color_target0_context_state(
                context_state(
                    address,
                    width,
                    height,
                    info,
                    swizzle,
                    resource_type,
                    target_mask));
    REQUIRE(decoded.has_value());
    return decoded.value();
}

}  // namespace

TEST_CASE(
    "GFX10 linear RGBA8 target maps decoded context state to typed image",
    "[graphics][gfx10][color-target][image]") {
    const auto plan =
        astraea::graphics::
            plan_gfx10_linear_rgba8_unorm_color_target_image(
                decoded_target());

    REQUIRE(plan.has_value());
    REQUIRE(plan->base_address.value == 0x1200U);
    REQUIRE(
        plan->format ==
        astraea::graphics::
            GuestGpuImageFormat::r8g8b8a8_unorm);
    REQUIRE(plan->width == 4U);
    REQUIRE(plan->height == 4U);
    REQUIRE(plan->bytes_per_pixel == 4U);
    REQUIRE(plan->logical_byte_count == 64U);
    REQUIRE(
        plan->layout.kind ==
        astraea::graphics::
            GuestGpuSurfaceLayoutKind::
                gfx10_aligned_linear);
    REQUIRE(plan->layout.pitch_pixels == 64U);
    REQUIRE(plan->layout.pitch_bytes == 256U);
    REQUIRE(plan->layout.base_alignment_bytes == 256U);
    REQUIRE(plan->layout.surface_byte_count == 1024U);
}

TEST_CASE(
    "GFX10 color-target image mapping is not hardcoded to 4x4 or write mask",
    "[graphics][gfx10][color-target][image][generic]") {
    const auto plan =
        astraea::graphics::
            plan_gfx10_linear_rgba8_unorm_color_target_image(
                decoded_target(
                    0x2000U,
                    8U,
                    3U,
                    astraea::graphics::
                        kGfx10ColorFormatR8G8B8A8 << 2U,
                    astraea::graphics::
                        kGfx10ColorSwizzleLinear,
                    astraea::graphics::
                        kGfx10ResourceType2d,
                    0x05U));

    REQUIRE(plan.has_value());
    REQUIRE(plan->width == 8U);
    REQUIRE(plan->height == 3U);
    REQUIRE(plan->logical_byte_count == 96U);
    REQUIRE(plan->layout.pitch_pixels == 64U);
    REQUIRE(plan->layout.pitch_bytes == 256U);
    REQUIRE(plan->layout.surface_byte_count == 768U);
}

TEST_CASE(
    "decoded GFX10 target registers only with complete physical backing",
    "[graphics][gfx10][color-target][image][registration]") {
    const auto plan =
        astraea::graphics::
            plan_gfx10_linear_rgba8_unorm_color_target_image(
                decoded_target());
    REQUIRE(plan.has_value());

    SECTION("complete backing") {
        astraea::graphics::GuestGpuAllocationAddressSpace
            allocations;
        const auto allocation =
            allocations.register_allocation(
                astraea::graphics::GpuVirtualAddress{
                    .value = 0x1000U},
                0x2000U);
        REQUIRE(allocation.has_value());

        astraea::graphics::GuestGpuImageRegistry images;
        const auto image =
            images.register_view(
                plan.value(),
                allocations);
        REQUIRE(image.has_value());

        const auto* view =
            images.entry_at(image.value());
        REQUIRE(view != nullptr);
        REQUIRE(
            view->allocation_id ==
            allocation.value());
        REQUIRE(view->allocation_byte_offset == 0x200U);
        REQUIRE(
            view->descriptor.logical_byte_count ==
            64U);
        REQUIRE(
            view->descriptor.layout.surface_byte_count ==
            1024U);
    }

    SECTION("logical-only backing") {
        astraea::graphics::GuestGpuAllocationAddressSpace
            allocations;
        REQUIRE(
            allocations.register_allocation(
                astraea::graphics::GpuVirtualAddress{
                    .value = 0x1200U},
                64U)
                .has_value());

        astraea::graphics::GuestGpuImageRegistry images;
        const auto image =
            images.register_view(
                plan.value(),
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
}

TEST_CASE(
    "GFX10 color-target image rejects unsupported format and layout semantics",
    "[graphics][gfx10][color-target][image][negative]") {
    using Error =
        astraea::graphics::
            Gfx10ColorTargetImageErrorCode;

    SECTION("format") {
        auto target = decoded_target();
        target.format = 9U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_format);
    }

    SECTION("number type") {
        auto target = decoded_target();
        target.number_type = 1U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_number_type);
    }

    SECTION("component swap") {
        auto target = decoded_target();
        target.component_swap = 1U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_component_swap);
    }

    SECTION("linear general") {
        auto target = decoded_target();
        target.raw_info |= 1U << 7U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::linear_general_not_supported);
    }

    SECTION("fast clear") {
        auto target = decoded_target();
        target.raw_info |= 1U << 13U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::fast_clear_not_supported);
    }

    SECTION("compression") {
        auto target = decoded_target();
        target.raw_info |= 1U << 14U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::compression_not_supported);
    }

    SECTION("FMASK state") {
        auto target = decoded_target();
        target.raw_info |= 1U << 27U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::fmask_not_supported);
    }

    SECTION("DCC") {
        auto target = decoded_target();
        target.dcc_enabled = true;
        target.raw_info |= 1U << 28U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::dcc_not_supported);
    }

    SECTION("CMASK") {
        auto target = decoded_target();
        target.raw_info |= 1U << 29U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::cmask_not_supported);
    }

    SECTION("NBC tiling") {
        auto target = decoded_target();
        target.raw_info |= 1U << 31U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::nbc_tiling_not_supported);
    }

    SECTION("swizzle") {
        auto target = decoded_target();
        target.color_sw_mode = 1U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_swizzle_mode);
    }

    SECTION("resource type") {
        auto target = decoded_target();
        target.resource_type = 0U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_resource_type);
    }

    SECTION("base alignment") {
        auto target = decoded_target(0x1200U);
        target.base_address.value = 0x1204U;
        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::base_address_misaligned);
    }
}

TEST_CASE(
    "GFX10 color-target image preserves checked size and layout failures",
    "[graphics][gfx10][color-target][image][negative][range]") {
    SECTION("logical size overflow") {
        auto target = decoded_target();
        target.width =
            std::numeric_limits<std::uint32_t>::max();
        target.height =
            std::numeric_limits<std::uint32_t>::max();

        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Gfx10ColorTargetImageErrorCode::
                    logical_size_overflow);
    }

    SECTION("layout failure") {
        auto target = decoded_target();
        target.width = 0U;

        const auto result =
            astraea::graphics::
                plan_gfx10_linear_rgba8_unorm_color_target_image(
                    target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Gfx10ColorTargetImageErrorCode::
                    surface_layout_failure);
        REQUIRE(
            result.error().
                surface_layout_error.has_value());
        REQUIRE(
            result.error().
                surface_layout_error->code ==
            astraea::graphics::
                Gfx10AlignedLinear2dLayoutErrorCode::
                    zero_dimension);
    }
}
