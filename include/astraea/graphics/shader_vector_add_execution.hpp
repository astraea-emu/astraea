#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_scalar_execution.hpp>
#include <astraea/graphics/shader_vector_execution.hpp>

namespace astraea::graphics {

struct ShaderVectorAddF32Effect {
    ShaderWaveSize wave_size = ShaderWaveSize::unspecified;
    std::uint8_t destination_vgpr = 0;
    std::uint8_t source0_vgpr = 0;
    std::uint8_t source1_vgpr = 0;
    std::uint64_t active_lane_mask = 0;
    std::array<std::uint32_t, kShaderMaxWaveLaneCount>
        written_values{};

    auto operator<=>(const ShaderVectorAddF32Effect&) const =
        default;
};

enum class ShaderVectorAddF32UnsupportedReason {
    non_normal_input,
    zero_result,
    inexact_result,
    non_normal_result,
};

enum class ShaderVectorAddF32ExecutionErrorCode {
    unsupported_operation,
    invalid_wave_size,
    unsupported_f32_case,
};

struct ShaderVectorAddF32ExecutionError {
    ShaderVectorAddF32ExecutionErrorCode code =
        ShaderVectorAddF32ExecutionErrorCode::
            unsupported_operation;
    std::optional<std::size_t> lane_index;
    std::optional<ShaderVectorAddF32UnsupportedReason>
        unsupported_reason;

    auto operator<=>(
        const ShaderVectorAddF32ExecutionError&) const = default;
};

using ShaderVectorAddF32ExecutionResult =
    astraea::core::Result<
        ShaderVectorAddF32Effect,
        ShaderVectorAddF32ExecutionError>;

// Executes only the mode-independent exact-normal subset of the already-typed
// plain VGPR + VGPR -> VGPR V_ADD_F32 operation.
//
// Every active lane must have two finite normal binary32 inputs whose
// mathematical sum is nonzero and exactly representable as a finite normal
// binary32 result. These cases require no rounding and no denormal handling, so
// they are invariant across documented RDNA2 FP_ROUND/FP_DENORM modes.
//
// The implementation uses integer bit arithmetic only. All active lanes are
// validated before any destination lane is mutated. Mode-sensitive cases remain
// explicitly unsupported until Astraea models floating-point MODE semantics.
[[nodiscard]] ShaderVectorAddF32ExecutionResult
execute_shader_vector_add_f32_exact_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state) noexcept;

}  // namespace astraea::graphics
