#include <astraea/research/v3_first_raster_target.hpp>

#include <cstdint>

#include <astraea/graphics/gfx10_surface_layout.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::ColorTarget0ContextState valid_target() {
    return astraea::graphics::ColorTarget0ContextState{
        .base_address =
            astraea::graphics::GpuVirtualAddress{
                .value = 0x0000123456789a00ULL,
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
    "first V3 raster target accepts exactly the bounded 4x4 linear RGBA8 state",
    "[research][v3][raster-target]") {
    const auto target = valid_target();

    const auto result =
        astraea::research::
            plan_v3_first_raster_target(target);

    REQUIRE(result.has_value());
    REQUIRE(result->base_address == target.base_address);
    REQUIRE(result->width == 4U);
    REQUIRE(result->height == 4U);
    REQUIRE(result->bytes_per_pixel == 4U);
    REQUIRE(result->logical_byte_count == 64U);
    REQUIRE(result->surface_pitch_pixels == 64U);
    REQUIRE(result->surface_pitch_bytes == 256U);
    REQUIRE(result->surface_base_alignment_bytes == 256U);
    REQUIRE(result->surface_byte_count == 1024U);
}

TEST_CASE(
    "first V3 raster target consumes the verified GFX10 layout result",
    "[research][v3][raster-target][layout]") {
    const auto generic =
        astraea::graphics::
            compute_gfx10_aligned_linear_2d_layout(
                4U,
                4U,
                4U);
    REQUIRE(generic.has_value());

    const auto result =
        astraea::research::
            plan_v3_first_raster_target(valid_target());
    REQUIRE(result.has_value());

    REQUIRE(
        result->surface_pitch_pixels ==
        generic->pitch_pixels);
    REQUIRE(
        result->surface_pitch_bytes ==
        generic->pitch_bytes);
    REQUIRE(
        result->surface_base_alignment_bytes ==
        generic->base_alignment_bytes);
    REQUIRE(
        result->surface_byte_count ==
        generic->surface_byte_count);
    REQUIRE(
        result->surface_byte_count >
        result->logical_byte_count);
}

TEST_CASE(
    "first V3 raster target rejects null and misaligned addresses",
    "[research][v3][raster-target][negative][address]") {
    SECTION("null") {
        auto target = valid_target();
        target.base_address.value = 0U;

        const auto result =
            astraea::research::
                plan_v3_first_raster_target(target);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    null_base_address);
    }

    SECTION("misaligned") {
        auto target = valid_target();
        target.base_address.value =
            0x0000123456789a04ULL;

        const auto result =
            astraea::research::
                plan_v3_first_raster_target(target);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    base_address_not_surface_aligned);
    }
}

TEST_CASE(
    "first V3 raster target rejects unsupported render-state fields",
    "[research][v3][raster-target][negative]") {
    SECTION("write mask") {
        auto target = valid_target();
        target.write_mask = 0x07U;
        const auto result =
            astraea::research::
                plan_v3_first_raster_target(target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_write_mask);
    }

    SECTION("format") {
        auto target = valid_target();
        target.format = 9U;
        const auto result =
            astraea::research::
                plan_v3_first_raster_target(target);
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
            astraea::research::
                plan_v3_first_raster_target(target);
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
            astraea::research::
                plan_v3_first_raster_target(target);
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
            astraea::research::
                plan_v3_first_raster_target(target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    dcc_not_supported);
    }
}

TEST_CASE(
    "first V3 raster target remains exact in dimensions and surface kind",
    "[research][v3][raster-target][negative][shape]") {
    SECTION("dimensions") {
        auto target = valid_target();
        target.width = 8U;

        const auto result =
            astraea::research::
                plan_v3_first_raster_target(target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_dimensions);
    }

    SECTION("swizzle") {
        auto target = valid_target();
        target.color_sw_mode = 1U;

        const auto result =
            astraea::research::
                plan_v3_first_raster_target(target);
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
            astraea::research::
                plan_v3_first_raster_target(target);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterTargetErrorCode::
                    unsupported_resource_type);
    }
}
