#include <astraea/research/v3_first_draw_plan.hpp>

#include <cstddef>

namespace astraea::research {
namespace {

[[nodiscard]] V3FirstDrawPlanError error(
    V3FirstDrawPlanErrorCode code,
    std::uint32_t actual_value = 0) noexcept {
    return V3FirstDrawPlanError{
        .code = code,
        .actual_value = actual_value,
    };
}

}  // namespace

V3FirstDrawPlanResult
plan_v3_first_draw(
    const astraea::graphics::UserConfigRegisterState& user_config_state,
    const astraea::graphics::GraphicsIrSetInstanceCount& instances,
    const astraea::graphics::GraphicsIrDrawIndexAuto& draw) noexcept {
    const auto primitive_index =
        static_cast<std::size_t>(
            kV3FirstDrawPrimitiveTypeUconfigOffset);
    if (!user_config_state.initialized.test(
            primitive_index)) {
        return V3FirstDrawPlanResult::failure(
            error(
                V3FirstDrawPlanErrorCode::
                    primitive_type_not_initialized));
    }

    const auto primitive_type =
        user_config_state.values[primitive_index];
    if (primitive_type !=
        kV3FirstDrawTriangleListPrimitiveType) {
        return V3FirstDrawPlanResult::failure(
            error(
                V3FirstDrawPlanErrorCode::
                    unsupported_primitive_type,
                primitive_type));
    }

    if (instances.instance_count !=
        kV3FirstDrawInstanceCount) {
        return V3FirstDrawPlanResult::failure(
            error(
                V3FirstDrawPlanErrorCode::
                    unsupported_instance_count,
                instances.instance_count));
    }

    if (draw.index_count !=
        kV3FirstDrawIndexCount) {
        return V3FirstDrawPlanResult::failure(
            error(
                V3FirstDrawPlanErrorCode::
                    unsupported_index_count,
                draw.index_count));
    }

    if (draw.initiator !=
        kV3FirstDrawAutoIndexInitiator) {
        return V3FirstDrawPlanResult::failure(
            error(
                V3FirstDrawPlanErrorCode::
                    unsupported_initiator,
                draw.initiator));
    }

    return V3FirstDrawPlanResult::success(
        V3FirstDrawPlan{
            .primitive_type = primitive_type,
            .instance_count =
                instances.instance_count,
            .index_count = draw.index_count,
            .initiator = draw.initiator,
        });
}

}  // namespace astraea::research
