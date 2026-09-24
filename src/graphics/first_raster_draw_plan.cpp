#include <astraea/graphics/first_raster_draw_plan.hpp>

#include <cstddef>

namespace astraea::graphics {
namespace {

[[nodiscard]] FirstRasterDrawPlanError error(
    FirstRasterDrawPlanErrorCode code,
    std::uint32_t actual_value = 0U) noexcept {
    return FirstRasterDrawPlanError{
        .code = code,
        .actual_value = actual_value,
    };
}

}  // namespace

FirstRasterDrawPlanResult
plan_first_raster_draw(
    const UserConfigRegisterState& user_config_state,
    const GraphicsIrSetInstanceCount& instances,
    const GraphicsIrDrawIndexAuto& draw) noexcept {
    const auto primitive_index =
        static_cast<std::size_t>(
            kFirstRasterPrimitiveTypeUconfigOffset);

    if (!user_config_state.initialized.test(
            primitive_index)) {
        return FirstRasterDrawPlanResult::failure(
            error(
                FirstRasterDrawPlanErrorCode::
                    primitive_type_not_initialized));
    }

    const auto primitive_type =
        user_config_state.values[primitive_index];
    if (primitive_type !=
        kFirstRasterPrimitiveTypeRaw) {
        return FirstRasterDrawPlanResult::failure(
            error(
                FirstRasterDrawPlanErrorCode::
                    unsupported_primitive_type,
                primitive_type));
    }

    if (instances.instance_count !=
        kFirstRasterInstanceCount) {
        return FirstRasterDrawPlanResult::failure(
            error(
                FirstRasterDrawPlanErrorCode::
                    unsupported_instance_count,
                instances.instance_count));
    }

    if (draw.index_count !=
        kFirstRasterIndexCount) {
        return FirstRasterDrawPlanResult::failure(
            error(
                FirstRasterDrawPlanErrorCode::
                    unsupported_index_count,
                draw.index_count));
    }

    if (draw.initiator !=
        kFirstRasterAutoIndexInitiatorRaw) {
        return FirstRasterDrawPlanResult::failure(
            error(
                FirstRasterDrawPlanErrorCode::
                    unsupported_initiator,
                draw.initiator));
    }

    return FirstRasterDrawPlanResult::success(
        FirstRasterDrawPlan{
            .primitive_type_raw = primitive_type,
            .instance_count =
                instances.instance_count,
            .index_count = draw.index_count,
            .initiator_raw = draw.initiator,
        });
}

}  // namespace astraea::graphics
