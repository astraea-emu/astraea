#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_ir.hpp>
#include <astraea/graphics/shader_scalar_execution.hpp>

namespace astraea::graphics {

enum class ShaderBranchDecisionKind {
    unconditional,
    conditional,
};

struct ShaderBranchDecision {
    ShaderBranchDecisionKind kind =
        ShaderBranchDecisionKind::unconditional;
    std::optional<ShaderIrBranchCondition> condition;
    std::int32_t byte_delta = 0;
    bool taken = false;

    auto operator<=>(const ShaderBranchDecision&) const = default;
};

enum class ShaderBranchEvaluationErrorCode {
    unsupported_operation,
    invalid_condition,
};

struct ShaderBranchEvaluationError {
    ShaderBranchEvaluationErrorCode code =
        ShaderBranchEvaluationErrorCode::unsupported_operation;

    auto operator<=>(const ShaderBranchEvaluationError&) const =
        default;
};

using ShaderBranchEvaluationResult =
    astraea::core::Result<
        ShaderBranchDecision,
        ShaderBranchEvaluationError>;

// Evaluates only the currently-typed Shader IR relative branch operations.
// The caller supplies generic RDNA2 scalar condition state explicitly. This
// function is pure: it does not mutate state, follow the branch, advance a
// program counter, or execute a CFG.
[[nodiscard]] ShaderBranchEvaluationResult
evaluate_shader_branch_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& state) noexcept;

}  // namespace astraea::graphics
