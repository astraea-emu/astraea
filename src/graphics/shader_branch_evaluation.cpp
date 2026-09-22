#include <astraea/graphics/shader_branch_evaluation.hpp>

#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderBranchEvaluationError failure(
    ShaderBranchEvaluationErrorCode code) noexcept {
    return ShaderBranchEvaluationError{
        .code = code,
    };
}

[[nodiscard]] std::optional<bool>
evaluate_condition(
    ShaderIrBranchCondition condition,
    const ShaderScalarState& state) noexcept {
    switch (condition) {
    case ShaderIrBranchCondition::scc_zero:
        return !state.scc;
    case ShaderIrBranchCondition::scc_one:
        return state.scc;
    case ShaderIrBranchCondition::vcc_zero:
        return state.vcc == 0;
    case ShaderIrBranchCondition::vcc_nonzero:
        return state.vcc != 0;
    case ShaderIrBranchCondition::exec_zero:
        return state.exec == 0;
    case ShaderIrBranchCondition::exec_nonzero:
        return state.exec != 0;
    }

    return std::nullopt;
}

}  // namespace

ShaderBranchEvaluationResult
evaluate_shader_branch_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& state) noexcept {
    if (const auto* branch =
            std::get_if<ShaderIrRelativeBranch>(
                &operation);
        branch != nullptr) {
        return ShaderBranchEvaluationResult::success(
            ShaderBranchDecision{
                .kind =
                    ShaderBranchDecisionKind::unconditional,
                .condition = std::nullopt,
                .byte_delta = branch->byte_delta,
                .taken = true,
            });
    }

    if (const auto* branch =
            std::get_if<
                ShaderIrConditionalRelativeBranch>(
                &operation);
        branch != nullptr) {
        const auto taken =
            evaluate_condition(
                branch->condition,
                state);
        if (!taken.has_value()) {
            return ShaderBranchEvaluationResult::failure(
                failure(
                    ShaderBranchEvaluationErrorCode::
                        invalid_condition));
        }

        return ShaderBranchEvaluationResult::success(
            ShaderBranchDecision{
                .kind =
                    ShaderBranchDecisionKind::conditional,
                .condition = branch->condition,
                .byte_delta = branch->byte_delta,
                .taken = taken.value(),
            });
    }

    return ShaderBranchEvaluationResult::failure(
        failure(
            ShaderBranchEvaluationErrorCode::
                unsupported_operation));
}

}  // namespace astraea::graphics
