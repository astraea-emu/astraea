#include <astraea/research/v3_first_draw_plan.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::UserConfigRegisterState triangle_state() {
    astraea::graphics::UserConfigRegisterState state{};
    state.values[
        astraea::research::
            kV3FirstDrawPrimitiveTypeUconfigOffset] =
        astraea::research::
            kV3FirstDrawTriangleListPrimitiveType;
    state.initialized.set(
        astraea::research::
            kV3FirstDrawPrimitiveTypeUconfigOffset);
    return state;
}

}  // namespace

TEST_CASE(
    "first V3 draw accepts exact observed synthetic triangle shape",
    "[research][v3][draw-plan]") {
    const auto result =
        astraea::research::plan_v3_first_draw(
            triangle_state(),
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
        astraea::research::V3FirstDrawPlan{
            .primitive_type = 4U,
            .instance_count = 1U,
            .index_count = 3U,
            .initiator = 2U,
        });
}

TEST_CASE(
    "first V3 draw requires primitive state to be initialized",
    "[research][v3][draw-plan][negative]") {
    const astraea::graphics::UserConfigRegisterState state{};

    const auto result =
        astraea::research::plan_v3_first_draw(
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
        astraea::research::
            V3FirstDrawPlanErrorCode::
                primitive_type_not_initialized);
}

TEST_CASE(
    "first V3 draw rejects unsupported primitive raw value",
    "[research][v3][draw-plan][negative][primitive]") {
    auto state = triangle_state();
    state.values[
        astraea::research::
            kV3FirstDrawPrimitiveTypeUconfigOffset] =
        6U;

    const auto result =
        astraea::research::plan_v3_first_draw(
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
        astraea::research::
            V3FirstDrawPlanErrorCode::
                unsupported_primitive_type);
    REQUIRE(result.error().actual_value == 6U);
}

TEST_CASE(
    "first V3 draw rejects unsupported instance count",
    "[research][v3][draw-plan][negative][instances]") {
    const auto result =
        astraea::research::plan_v3_first_draw(
            triangle_state(),
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
        astraea::research::
            V3FirstDrawPlanErrorCode::
                unsupported_instance_count);
    REQUIRE(result.error().actual_value == 2U);
}

TEST_CASE(
    "first V3 draw rejects unsupported vertex count",
    "[research][v3][draw-plan][negative][vertices]") {
    const auto result =
        astraea::research::plan_v3_first_draw(
            triangle_state(),
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
        astraea::research::
            V3FirstDrawPlanErrorCode::
                unsupported_index_count);
    REQUIRE(result.error().actual_value == 4U);
}

TEST_CASE(
    "first V3 draw requires exact observed raw auto-index initiator",
    "[research][v3][draw-plan][negative][initiator]") {
    const auto result =
        astraea::research::plan_v3_first_draw(
            triangle_state(),
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
        astraea::research::
            V3FirstDrawPlanErrorCode::
                unsupported_initiator);
    REQUIRE(
        result.error().actual_value ==
        0xa5a50002U);
}
