#include <astraea/graphics/first_raster_draw_plan.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::UserConfigRegisterState
accepted_state() {
    astraea::graphics::UserConfigRegisterState state{};
    state.values[
        astraea::graphics::
            kFirstRasterPrimitiveTypeUconfigOffset] =
        astraea::graphics::
            kFirstRasterPrimitiveTypeRaw;
    state.initialized.set(
        astraea::graphics::
            kFirstRasterPrimitiveTypeUconfigOffset);
    return state;
}

}  // namespace

TEST_CASE(
    "first raster draw accepts only the exact owned raw profile",
    "[graphics][v3][draw-plan]") {
    const auto result =
        astraea::graphics::
            plan_first_raster_draw(
                accepted_state(),
                astraea::graphics::
                    GraphicsIrSetInstanceCount{
                        .instance_count = 1U,
                    },
                astraea::graphics::
                    GraphicsIrDrawIndexAuto{
                        .index_count = 3U,
                        .initiator = 2U,
                    });

    REQUIRE(result.has_value());
    REQUIRE(
        result.value() ==
        astraea::graphics::FirstRasterDrawPlan{
            .primitive_type_raw = 4U,
            .instance_count = 1U,
            .index_count = 3U,
            .initiator_raw = 2U,
        });
}

TEST_CASE(
    "first raster draw rejects unsupported raw state explicitly",
    "[graphics][v3][draw-plan][negative]") {
    using Error =
        astraea::graphics::
            FirstRasterDrawPlanErrorCode;

    SECTION("primitive uninitialized") {
        const astraea::graphics::
            UserConfigRegisterState state{};
        const auto result =
            astraea::graphics::
                plan_first_raster_draw(
                    state,
                    astraea::graphics::
                        GraphicsIrSetInstanceCount{
                            .instance_count = 1U,
                        },
                    astraea::graphics::
                        GraphicsIrDrawIndexAuto{
                            .index_count = 3U,
                            .initiator = 2U,
                        });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::primitive_type_not_initialized);
    }

    SECTION("primitive raw value") {
        auto state = accepted_state();
        state.values[
            astraea::graphics::
                kFirstRasterPrimitiveTypeUconfigOffset] =
            6U;
        const auto result =
            astraea::graphics::
                plan_first_raster_draw(
                    state,
                    astraea::graphics::
                        GraphicsIrSetInstanceCount{
                            .instance_count = 1U,
                        },
                    astraea::graphics::
                        GraphicsIrDrawIndexAuto{
                            .index_count = 3U,
                            .initiator = 2U,
                        });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_primitive_type);
        REQUIRE(result.error().actual_value == 6U);
    }

    SECTION("instances") {
        const auto result =
            astraea::graphics::
                plan_first_raster_draw(
                    accepted_state(),
                    astraea::graphics::
                        GraphicsIrSetInstanceCount{
                            .instance_count = 2U,
                        },
                    astraea::graphics::
                        GraphicsIrDrawIndexAuto{
                            .index_count = 3U,
                            .initiator = 2U,
                        });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_instance_count);
        REQUIRE(result.error().actual_value == 2U);
    }

    SECTION("index count") {
        const auto result =
            astraea::graphics::
                plan_first_raster_draw(
                    accepted_state(),
                    astraea::graphics::
                        GraphicsIrSetInstanceCount{
                            .instance_count = 1U,
                        },
                    astraea::graphics::
                        GraphicsIrDrawIndexAuto{
                            .index_count = 4U,
                            .initiator = 2U,
                        });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_index_count);
        REQUIRE(result.error().actual_value == 4U);
    }

    SECTION("initiator raw value") {
        const auto result =
            astraea::graphics::
                plan_first_raster_draw(
                    accepted_state(),
                    astraea::graphics::
                        GraphicsIrSetInstanceCount{
                            .instance_count = 1U,
                        },
                    astraea::graphics::
                        GraphicsIrDrawIndexAuto{
                            .index_count = 3U,
                            .initiator = 0xa5a50002U,
                        });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_initiator);
        REQUIRE(
            result.error().actual_value ==
            0xa5a50002U);
    }
}
