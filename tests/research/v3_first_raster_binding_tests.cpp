#include <astraea/research/v3_first_raster_binding.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::ColorTarget0ContextState
valid_target(
    std::uint64_t base = 0x0000123456789a00ULL) {
    return astraea::graphics::ColorTarget0ContextState{
        .base_address =
            astraea::graphics::GpuVirtualAddress{
                .value = base,
            },
        .raw_target_mask = 0x0fU,
        .write_mask = 0x0fU,
        .raw_info = 10U << 2U,
        .format = 10U,
        .number_type = 0U,
        .component_swap = 0U,
        .dcc_enabled = false,
        .raw_attrib2 = (3U << 14U) | 3U,
        .width = 4U,
        .height = 4U,
        .raw_attrib3 = 1U << 24U,
        .color_sw_mode = 0U,
        .resource_type = 1U,
    };
}

}  // namespace

TEST_CASE(
    "first V3 raster target derives physical GFX10 backing from raw target state",
    "[research][v3][raster-binding]") {
    const auto result =
        astraea::research::plan_v3_first_raster_target(
            valid_target());

    REQUIRE(result.has_value());
    REQUIRE(result->image.width == 4U);
    REQUIRE(result->image.height == 4U);
    REQUIRE(result->image.bytes_per_pixel == 4U);
    REQUIRE(result->image.logical_byte_count == 64U);
    REQUIRE(
        result->image.format ==
        astraea::graphics::GuestGpuImageFormat::
            r8g8b8a8_unorm);
    REQUIRE(
        result->image.layout.kind ==
        astraea::graphics::GuestGpuSurfaceLayoutKind::
            gfx10_aligned_linear);
    REQUIRE(result->image.layout.pitch_pixels == 64U);
    REQUIRE(result->image.layout.pitch_bytes == 256U);
    REQUIRE(
        result->image.layout.base_alignment_bytes ==
        256U);
    REQUIRE(
        result->image.layout.surface_byte_count ==
        1024U);
    REQUIRE(
        result->image.layout.surface_byte_count >
        result->image.logical_byte_count);
}

TEST_CASE(
    "first V3 raster target rejects every non-exact profile component",
    "[research][v3][raster-binding][negative]") {
    SECTION("null base") {
        auto target = valid_target(0U);
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    null_base_address);
    }

    SECTION("write mask") {
        auto target = valid_target();
        target.write_mask = 0x07U;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_write_mask);
    }

    SECTION("color format") {
        auto target = valid_target();
        target.format = 9U;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_color_format);
    }

    SECTION("number type") {
        auto target = valid_target();
        target.number_type = 1U;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_number_type);
    }

    SECTION("component swap") {
        auto target = valid_target();
        target.component_swap = 2U;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_component_swap);
    }

    SECTION("DCC") {
        auto target = valid_target();
        target.dcc_enabled = true;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    dcc_not_supported);
    }

    SECTION("dimensions") {
        auto target = valid_target();
        target.width = 8U;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_dimensions);
    }

    SECTION("swizzle mode") {
        auto target = valid_target();
        target.color_sw_mode = 1U;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_color_swizzle_mode);
    }

    SECTION("resource type") {
        auto target = valid_target();
        target.resource_type = 0U;
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_resource_type);
    }

    SECTION("base alignment") {
        auto target =
            valid_target(0x0000123456789a04ULL);
        const auto result =
            astraea::research::plan_v3_first_raster_target(
                target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    base_address_misaligned);
    }
}

TEST_CASE(
    "first V3 raster image resolves complete physical backing",
    "[research][v3][raster-binding][image]") {
    const auto plan =
        astraea::research::plan_v3_first_raster_target(
            valid_target(0x1200U));
    REQUIRE(plan.has_value());

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
        astraea::research::register_v3_first_raster_image(
            plan.value(),
            allocations,
            images);

    REQUIRE(image.has_value());
    const auto* view =
        images.entry_at(image.value());
    REQUIRE(view != nullptr);
    REQUIRE(view->allocation_id == allocation.value());
    REQUIRE(view->allocation_byte_offset == 0x200U);
    REQUIRE(view->descriptor.logical_byte_count == 64U);
    REQUIRE(
        view->descriptor.layout.surface_byte_count ==
        1024U);
}

TEST_CASE(
    "first V3 raster image rejects logical-only backing",
    "[research][v3][raster-binding][image][negative]") {
    const auto plan =
        astraea::research::plan_v3_first_raster_target(
            valid_target(0x1200U));
    REQUIRE(plan.has_value());

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
        astraea::research::register_v3_first_raster_image(
            plan.value(),
            allocations,
            images);

    REQUIRE_FALSE(image.has_value());
    REQUIRE(
        image.error().code ==
        astraea::research::
            V3FirstRasterImageErrorCode::
                image_registration_failure);
    REQUIRE(
        image.error().registration_error.code ==
        astraea::graphics::
            GuestGpuImageRegistrationErrorCode::
                allocation_resolution_failure);
    REQUIRE(
        image.error().
            registration_error.
            allocation_resolution_error.has_value());
    REQUIRE(
        image.error().
            registration_error.
            allocation_resolution_error->code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                partially_mapped_range);
    REQUIRE(images.size() == 0U);
}
