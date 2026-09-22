#include <astraea/graphics/shader_control_execution.hpp>

namespace astraea::graphics {

ShaderBranchDecision
evaluate_shader_branch_condition(
    ShaderIrBranchCondition condition,
    const ShaderScalarState& state) noexcept {
    bool taken = false;

    switch (condition) {
    case ShaderIrBranchCondition::scc_zero:
        taken = !state.scc;
        break;
    case ShaderIrBranchCondition::scc_one:
        taken = state.scc;
        break;
    case ShaderIrBranchCondition::vcc_zero:
        taken = state.vcc == 0;
        break;
    case ShaderIrBranchCondition::vcc_nonzero:
        taken = state.vcc != 0;
        break;
    case ShaderIrBranchCondition::exec_zero:
        taken = state.exec == 0;
        break;
    case ShaderIrBranchCondition::exec_nonzero:
        taken = state.exec != 0;
        break;
    }

    return ShaderBranchDecision{
        .condition = condition,
        .taken = taken,
    };
}

}  // namespace astraea::graphics
