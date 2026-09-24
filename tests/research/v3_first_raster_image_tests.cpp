#include <astraea/research/v3_first_raster_image.hpp>

#include <cstdint>

#include <astraea/research/v3_first_raster_target.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::ColorTarget0ContextState valid_target() {
    return astraea::graphics::ColorTarget0ContextState{
        .base_address =
            astraea::graphics::GpuVirtualAddress{
                .value = 0x0000000000001200ULL,
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

astraea::research::V3FirstRasterTargetPlan
valid_plan() {
    const auto result =
        astraea::research::
            plan_v3_first_raster_target(valid_target());
    REQUIRE(result.has_value());
    return result.value();
}

}  // namespace

TEST_CASE(
    "V3 first raster plan registers one exact typed guest image view",
    "[research][v3][raster-image]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;

    const auto allocation =
        allocations.register_allocation(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x1000U,
            },
            0x2000U);
    REQUIRE(allocation.has_value());

    const auto result =
        astraea::research::
            register_v3_first_raster_image(
                valid_plan(),
                allocations,
                images);

    REQUIRE(result.has_value());
    const auto* view=images.entry_at(result.value());
    REQUIRE(view != nullptr);
    REQUIRE(view->allocation_id == allocation.value());
    REQUIRE(view->allocation_byte_offset == 0x200U);
    REQUIRE(view->descriptor.width == 4U);
    REQUIRE(view->descriptor.height == 4U);
    REQUIRE(view->descriptor.logical_byte_count == 64U);
    REQUIRE(view->descriptor.layout.pitch_pixels == 64U);
    REQUIRE(view->descriptor.layout.pitch_bytes == 256U);
    REQUIRE(
        view->descriptor.layout.surface_byte_count ==
        1024U);
}

TEST_CASE(
    "V3 first raster image requires complete physical surface backing",
    "[research][v3][raster-image][negative][backing]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;

    REQUIRE(
        allocations.register_allocation(
                       astraea::graphics::GpuVirtualAddress{
                           .value = 0x1200U,
                       },
                       64U)
            .has_value());

    const auto result =
        astraea::research::
            register_v3_first_raster_image(
                valid_plan(),
                allocations,
                images);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::research::
            V3FirstRasterImageErrorCode::
                image_registration_failure);
    REQUIRE(
        result.error().image_registration_error.has_value());
    REQUIRE(
        result.error().image_registration_error->code ==
        astraea::graphics::
            GuestGpuImageRegistrationErrorCode::
                allocation_resolution_failure);
    REQUIRE(images.size() == 0U);
}

TEST_CASE(
    "V3 first raster image rejects a tampered surface plan before registration",
    "[research][v3][raster-image][negative][plan]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;

    REQUIRE(
        allocations.register_allocation(
                       astraea::graphics::GpuVirtualAddress{
                           .value = 0x1000U,
                       },
                       0x2000U)
            .has_value());

    SECTION("stale 64-byte backing assumption") {
        auto plan = valid_plan();
        plan.surface_pitch_pixels = 4U;
        plan.surface_pitch_bytes = 16U;
        plan.surface_byte_count = 64U;

        const auto result =
            astraea::research::
                register_v3_first_raster_image(
                    plan,
                    allocations,
                    images);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterImageErrorCode::
                    raster_target_plan_mismatch);
        REQUIRE(images.size() == 0U);
    }

    SECTION("misaligned base") {
        auto plan = valid_plan();
        plan.base_address.value += 4U;

        const auto result =
            astraea::research::
                register_v3_first_raster_image(
                    plan,
                    allocations,
                    images);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstRasterImageErrorCode::
                    raster_target_plan_mismatch);
        REQUIRE(images.size() == 0U);
    }
}
