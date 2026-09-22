#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_control_execution.hpp>
#include <astraea/graphics/shader_vector_execution.hpp>

namespace astraea::graphics {

using ShaderWaveExecutionEffect =
    std::variant<
        ShaderScalarExecutionEffect,
        ShaderVectorMove32Effect,
        ShaderVectorAddF32Effect>;

struct ShaderWaveBlockExecution {
    std::size_t block_index = 0;
    std::size_t executed_emission_count = 0;
    std::vector<ShaderWaveExecutionEffect> effects;
    std::optional<ShaderBranchDecision> branch_decision;
    ShaderCfgSuccessorSelection successor;

    auto operator<=>(const ShaderWaveBlockExecution&) const =
        default;
};

enum class ShaderWaveBlockExecutionErrorCode {
    graph_program_mismatch,
    block_index_out_of_bounds,
    invalid_block_extent,
    invalid_block_control_flow,
    host_allocation_failure,
    unsupported_operation,
    scalar_execution_failure,
    vector_execution_failure,
    cfg_successor_failure,
};

struct ShaderWaveBlockExecutionError {
    ShaderWaveBlockExecutionErrorCode code =
        ShaderWaveBlockExecutionErrorCode::
            unsupported_operation;
    std::size_t block_index = 0;
    std::size_t emission_index = 0;
    std::size_t completed_emission_count = 0;
    std::optional<ShaderScalarExecutionError> scalar_error;
    std::optional<ShaderVectorExecutionError> vector_error;
    std::optional<ShaderCfgSuccessorError> successor_error;

    auto operator<=>(const ShaderWaveBlockExecutionError&) const =
        default;
};

using ShaderWaveBlockExecutionResult =
    astraea::core::Result<
        ShaderWaveBlockExecution,
        ShaderWaveBlockExecutionError>;

// Executes exactly one validated basic block against explicit generic scalar
// and vector wave state. Supported scalar and vector effects are returned in
// source order.
//
// Earlier successful writes remain applied if a later emission fails. The
// selected successor is returned but never executed. This function does not
// infer PS5 launch state, broaden V_ADD_F32 beyond the exact mode-independent
// subset, or execute memory, barriers, or waits.
[[nodiscard]] ShaderWaveBlockExecutionResult
execute_shader_wave_block(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t block_index,
    ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state);

}  // namespace astraea::graphics
