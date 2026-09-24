#include <astraea/graphics/gfx10_surface_layout.hpp>

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "GFX10 aligned-linear 4x4 RGBA8 has 256-byte rows and 1024-byte surface",
    "[graphics][gfx10][surface-layout][linear]") {
    const auto result =
        astraea::graphics::
            compute_gfx10_aligned_linear_2d_layout(
                4U,
                4U,
                4U);

    REQUIRE(result.has_value());
    REQUIRE(result->pitch_alignment_pixels == 64U);
    REQUIRE(result->pitch_pixels == 64U);
    REQUIRE(result->pitch_bytes == 256U);
    REQUIRE(result->base_alignment_bytes == 256U);
    REQUIRE(result->surface_byte_count == 1024U);
}

TEST_CASE(
    "GFX10 aligned-linear pitch rounds up to the next alignment unit",
    "[graphics][gfx10][surface-layout][linear]") {
    const auto result =
        astraea::graphics::
            compute_gfx10_aligned_linear_2d_layout(
                65U,
                2U,
                4U);

    REQUIRE(result.has_value());
    REQUIRE(result->pitch_alignment_pixels == 64U);
    REQUIRE(result->pitch_pixels == 128U);
    REQUIRE(result->pitch_bytes == 512U);
    REQUIRE(result->surface_byte_count == 1024U);
}

TEST_CASE(
    "GFX10 aligned-linear element size controls pitch alignment",
    "[graphics][gfx10][surface-layout][linear]") {
    const auto one_byte =
        astraea::graphics::
            compute_gfx10_aligned_linear_2d_layout(
                1U,
                1U,
                1U);
    const auto sixteen_byte =
        astraea::graphics::
            compute_gfx10_aligned_linear_2d_layout(
                1U,
                1U,
                16U);

    REQUIRE(one_byte.has_value());
    REQUIRE(one_byte->pitch_alignment_pixels == 256U);
    REQUIRE(one_byte->pitch_bytes == 256U);

    REQUIRE(sixteen_byte.has_value());
    REQUIRE(sixteen_byte->pitch_alignment_pixels == 16U);
    REQUIRE(sixteen_byte->pitch_bytes == 256U);
}

TEST_CASE(
    "GFX10 aligned-linear layout rejects unsupported shapes explicitly",
    "[graphics][gfx10][surface-layout][linear][negative]") {
    SECTION("zero dimensions") {
        const auto result =
            astraea::graphics::
                compute_gfx10_aligned_linear_2d_layout(
                    0U,
                    4U,
                    4U);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Gfx10AlignedLinear2dLayoutErrorCode::
                    zero_dimension);
    }

    SECTION("zero element size") {
        const auto result =
            astraea::graphics::
                compute_gfx10_aligned_linear_2d_layout(
                    4U,
                    4U,
                    0U);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Gfx10AlignedLinear2dLayoutErrorCode::
                    zero_element_size);
    }

    SECTION("element size not dividing 256") {
        const auto result =
            astraea::graphics::
                compute_gfx10_aligned_linear_2d_layout(
                    4U,
                    4U,
                    3U);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Gfx10AlignedLinear2dLayoutErrorCode::
                    unsupported_element_size);
    }

    SECTION("pitch overflow") {
        const auto result =
            astraea::graphics::
                compute_gfx10_aligned_linear_2d_layout(
                    std::numeric_limits<std::uint32_t>::max(),
                    1U,
                    4U);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Gfx10AlignedLinear2dLayoutErrorCode::
                    pitch_overflow);
    }
}
