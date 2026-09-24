#include <astraea/graphics/shader_export_execution.hpp>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderExportCaptureError export_error(
    ShaderExportCaptureErrorCode code) noexcept {
    return ShaderExportCaptureError{
        .code = code,
    };
}

}  // namespace

ShaderExportCaptureResult
capture_shader_export_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& scalar_state,
    const ShaderVectorState& vector_state) noexcept {
    const auto* export_op =
        std::get_if<ShaderIrExport>(&operation);
    if (export_op == nullptr) {
        return ShaderExportCaptureResult::failure(
            export_error(
                ShaderExportCaptureErrorCode::
                    unsupported_operation));
    }

    std::size_t lane_count = 0U;
    std::uint64_t active_lane_mask = 0U;
    switch (vector_state.wave_size) {
    case ShaderWaveSize::wave32:
        lane_count = 32U;
        active_lane_mask =
            scalar_state.exec & 0xffffffffULL;
        break;
    case ShaderWaveSize::wave64:
        lane_count = 64U;
        active_lane_mask = scalar_state.exec;
        break;
    case ShaderWaveSize::unspecified:
        return ShaderExportCaptureResult::failure(
            export_error(
                ShaderExportCaptureErrorCode::
                    invalid_wave_size));
    }

    ShaderExportCaptureEffect effect{
        .wave_size = vector_state.wave_size,
        .target_kind = export_op->target_kind,
        .target_index = export_op->target_index,
        .enable_mask = export_op->enable_mask,
        .compressed = export_op->compressed,
        .done = export_op->done,
        .valid_mask = export_op->valid_mask,
        .active_lane_mask = active_lane_mask,
        .source_values = {},
    };

    for (std::size_t component = 0U;
         component < export_op->sources.size();
         ++component) {
        const auto source_index =
            static_cast<std::size_t>(
                export_op->sources[component].index);
        for (std::size_t lane = 0U;
             lane < lane_count;
             ++lane) {
            const auto lane_bit =
                std::uint64_t{1} << lane;
            if ((active_lane_mask & lane_bit) == 0U) {
                continue;
            }
            effect.source_values[component][lane] =
                vector_state.vgprs[source_index][lane];
        }
    }

    return ShaderExportCaptureResult::success(
        effect);
}

}  // namespace astraea::graphics
