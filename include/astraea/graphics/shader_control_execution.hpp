#pragma once

#include <compare>
#include <cstddef>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_cfg.hpp>
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

struct ShaderCfgSuccessorSelection {
    std::optional<ShaderCfgEdge> edge;

    auto operator<=>(const ShaderCfgSuccessorSelection&) const =
        default;
};

enum class ShaderCfgSuccessorErrorCode {
    graph_program_mismatch,
    block_index_out_of_bounds,
    invalid_block_extent,
    invalid_edge_target,
    invalid_successor_topology,
    branch_decision_required,
    unexpected_branch_decision,
    branch_condition_mismatch,
};

struct ShaderCfgSuccessorError {
    ShaderCfgSuccessorErrorCode code =
        ShaderCfgSuccessorErrorCode::invalid_successor_topology;
    std::size_t block_index = 0;
    std::optional<std::size_t> edge_index;

    auto operator<=>(const ShaderCfgSuccessorError&) const =
        default;
};

using ShaderCfgSuccessorResult =
    astraea::core::Result<
        ShaderCfgSuccessorSelection,
        ShaderCfgSuccessorError>;

// Selects one already-validated CFG successor for a source block. The Shader IR
// program is supplied so the block terminator and a conditional decision's
// predicate identity can be checked against the CFG topology.
//
// This function does not mutate shader state, execute the target block, advance
// a persistent program counter, walk loops, or infer PS5 shader-entry state.
[[nodiscard]] ShaderCfgSuccessorResult
select_shader_cfg_successor(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t block_index,
    std::optional<ShaderBranchDecision> branch_decision) noexcept;

}  // namespace astraea::graphics
