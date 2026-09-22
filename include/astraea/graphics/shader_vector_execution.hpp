#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_ir.hpp>
#include <astraea/graphics/shader_scalar_execution.hpp>

namespace astraea::graphics {

inline constexpr std::size_t kShaderVectorGprCount = 256;
inline constexpr std::size_t kShaderMaxWaveLaneCount = 64;

enum class ShaderWaveSize : std::uint8_t {
    unspecified = 0,
    wave32 = 32,
    wave64 = 64,
};

struct ShaderVectorState {
    ShaderWaveSize wave_size = ShaderWaveSize::unspecified;
    std::array<
        std::array<std::uint32_t, kShaderMaxWaveLaneCount>,
        kShaderVectorGprCount>
        vgprs{};

    auto operator<=>(const ShaderVectorState&) const = default;
};

struct ShaderVectorMove32Effect {
    ShaderWaveSize wave_size = ShaderWaveSize::unspecified;
    std::uint8_t destination_vgpr = 0;
    std::uint8_t source_vgpr = 0;
    std::uint64_t active_lane_mask = 0;
    std::array<std::uint32_t, kShaderMaxWaveLaneCount>
        written_values{};

    auto operator<=>(const ShaderVectorMove32Effect&) const =
        default;
};

enum class ShaderVectorExecutionErrorCode {
    unsupported_operation,
    invalid_wave_size,
};

struct ShaderVectorExecutionError {
    ShaderVectorExecutionErrorCode code =
        ShaderVectorExecutionErrorCode::unsupported_operation;

    auto operator<=>(const ShaderVectorExecutionError&) const =
        default;
};

using ShaderVectorMove32ExecutionResult =
    astraea::core::Result<
        ShaderVectorMove32Effect,
        ShaderVectorExecutionError>;

// Executes only the currently-supported plain VGPR-to-VGPR V_MOV_B32 Shader IR
// operation. Wave size is explicit generic RDNA2 state, while EXEC is supplied
// from the existing scalar state. Inactive destination lanes are preserved.
//
// This function does not infer PS5 launch state, execute V_ADD_F32, interpret
// DPP/SDWA/other operand forms, or mutate scalar condition state.
[[nodiscard]] ShaderVectorMove32ExecutionResult
execute_shader_vector_move32_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state) noexcept;

}  // namespace astraea::graphics
