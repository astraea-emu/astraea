#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <vector>

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

struct ShaderScalarBlockExecution {
    std::size_t block_index = 0;
    std::size_t executed_emission_count = 0;
    std::vector<ShaderScalarExecutionEffect> scalar_write_effects;
    std::optional<ShaderBranchDecision> branch_decision;
    ShaderCfgSuccessorSelection successor;

    auto operator<=>(const ShaderScalarBlockExecution&) const =
        default;
};

enum class ShaderScalarBlockExecutionErrorCode {
    graph_program_mismatch,
    block_index_out_of_bounds,
    invalid_block_extent,
    invalid_block_control_flow,
    host_allocation_failure,
    unsupported_operation,
    scalar_execution_failure,
    cfg_successor_failure,
};

struct ShaderScalarBlockExecutionError {
    ShaderScalarBlockExecutionErrorCode code =
        ShaderScalarBlockExecutionErrorCode::
            unsupported_operation;
    std::size_t block_index = 0;
    std::size_t emission_index = 0;
    std::size_t completed_emission_count = 0;
    std::optional<ShaderScalarExecutionError> scalar_error;
    std::optional<ShaderCfgSuccessorError> successor_error;

    auto operator<=>(
        const ShaderScalarBlockExecutionError&) const = default;
};

using ShaderScalarBlockExecutionResult =
    astraea::core::Result<
        ShaderScalarBlockExecution,
        ShaderScalarBlockExecutionError>;

// Executes exactly one validated basic block against explicit generic scalar
// state. Earlier successful scalar writes remain applied if a later emission
// fails; completed_emission_count makes that non-atomic progress explicit.
//
// The selected successor is returned but never executed. This function does not
// maintain a cross-block PC, walk loops, execute vector/wait/barrier semantics,
// or infer any PS5 shader-entry state.
[[nodiscard]] ShaderScalarBlockExecutionResult
execute_shader_scalar_block(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t block_index,
    ShaderScalarState& state);

}  // namespace astraea::graphics
