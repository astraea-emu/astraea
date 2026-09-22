#pragma once

#include <compare>

#include <astraea/graphics/shader_ir.hpp>
#include <astraea/graphics/shader_scalar_execution.hpp>

namespace astraea::graphics {

struct ShaderBranchDecision {
    ShaderIrBranchCondition condition =
        ShaderIrBranchCondition::scc_zero;
    bool taken = false;

    auto operator<=>(const ShaderBranchDecision&) const = default;
};

// Evaluates only the already-typed generic RDNA2 branch predicate against
// explicit caller-supplied scalar state. This does not update a PC, choose a
// CFG edge, execute a block, or infer any PS5 shader-entry state.
[[nodiscard]] ShaderBranchDecision
evaluate_shader_branch_condition(
    ShaderIrBranchCondition condition,
    const ShaderScalarState& state) noexcept;

}  // namespace astraea::graphics
