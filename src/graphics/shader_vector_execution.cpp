#include <astraea/graphics/shader_vector_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderVectorExecutionError vector_error(
    ShaderVectorExecutionErrorCode code) noexcept {
    return ShaderVectorExecutionError{
        .code = code,
    };
}

}  // namespace

ShaderVectorMove32ExecutionResult
execute_shader_vector_move32_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state) noexcept {
    const auto* move =
        std::get_if<ShaderIrVectorMove32>(
            &operation);
    if (move == nullptr) {
        return ShaderVectorMove32ExecutionResult::failure(
            vector_error(
                ShaderVectorExecutionErrorCode::
                    unsupported_operation));
    }

    std::size_t lane_count = 0;
    std::uint64_t active_lane_mask = 0;
    switch (vector_state.wave_size) {
    case ShaderWaveSize::wave32:
        lane_count = 32;
        active_lane_mask =
            scalar_state.exec & 0xffffffffULL;
        break;
    case ShaderWaveSize::wave64:
        lane_count = 64;
        active_lane_mask = scalar_state.exec;
        break;
    case ShaderWaveSize::unspecified:
        return ShaderVectorMove32ExecutionResult::failure(
            vector_error(
                ShaderVectorExecutionErrorCode::
                    invalid_wave_size));
    }

    std::array<
        std::uint32_t,
        kShaderMaxWaveLaneCount>
        written_values{};

    const auto source_index =
        static_cast<std::size_t>(
            move->source.index);
    const auto destination_index =
        static_cast<std::size_t>(
            move->destination.index);

    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) != 0) {
            written_values[lane] =
                vector_state.vgprs[source_index][lane];
        }
    }

    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) != 0) {
            vector_state.vgprs[destination_index][lane] =
                written_values[lane];
        }
    }

    return ShaderVectorMove32ExecutionResult::success(
        ShaderVectorMove32Effect{
            .wave_size = vector_state.wave_size,
            .destination_vgpr =
                move->destination.index,
            .source_vgpr = move->source.index,
            .active_lane_mask = active_lane_mask,
            .written_values = written_values,
        });
}

}  // namespace astraea::graphics
