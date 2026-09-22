#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_ir.hpp>

namespace astraea::graphics {

// AMD RDNA2 ISA 70648 section 5.2 assigns scalar operand selectors
// 0-105 to SGPR0-SGPR105. This explicit state is generic RDNA2 test
// state; it does not describe a PS5 shader launch ABI.
inline constexpr std::size_t kShaderScalarGprCount = 106;

struct ShaderScalarState {
    std::array<std::uint32_t, kShaderScalarGprCount> sgprs{};
    std::uint64_t vcc = 0;
    std::uint32_t m0 = 0;
    std::uint64_t exec = 0;
    bool scc = false;

    auto operator<=>(const ShaderScalarState&) const = default;
};

enum class ShaderScalarWriteWidth {
    bits32,
    bits64,
};

struct ShaderScalarExecutionEffect {
    ShaderScalarWriteWidth width =
        ShaderScalarWriteWidth::bits32;
    std::uint8_t first_destination_sgpr = 0;
    std::array<std::uint32_t, 2> written_values{};

    auto operator<=>(
        const ShaderScalarExecutionEffect&) const = default;
};

enum class ShaderScalarExecutionErrorCode {
    unsupported_operation,
    invalid_sgpr_index,
    invalid_sgpr_pair,
};

enum class ShaderScalarExecutionOperandRole {
    none,
    source,
    destination,
};

struct ShaderScalarExecutionError {
    ShaderScalarExecutionErrorCode code =
        ShaderScalarExecutionErrorCode::unsupported_operation;
    ShaderScalarExecutionOperandRole role =
        ShaderScalarExecutionOperandRole::none;
    std::uint16_t sgpr_index = 0;

    auto operator<=>(
        const ShaderScalarExecutionError&) const = default;
};

using ShaderScalarExecutionResult =
    astraea::core::Result<
        ShaderScalarExecutionEffect,
        ShaderScalarExecutionError>;

// Executes only the currently-supported scalar move Shader IR operations.
// The caller supplies all scalar state explicitly. No Sony/PS5 stage-entry
// state, control-flow execution, vector lanes, memory semantics, or host
// backend behavior is inferred here.
//
// Failures are atomic: state is not modified unless the operation is fully
// validated and its source value(s) have been captured.
[[nodiscard]] ShaderScalarExecutionResult
execute_shader_scalar_operation(
    const ShaderIrOperation& operation,
    ShaderScalarState& state) noexcept;

}  // namespace astraea::graphics
