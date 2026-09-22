#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_control_execution.hpp>

namespace astraea::graphics {

struct ShaderScalarProgramExecution {
    std::size_t entry_block_index = 0;
    std::vector<ShaderScalarBlockExecution> block_executions;

    auto operator<=>(const ShaderScalarProgramExecution&) const =
        default;
};

enum class ShaderScalarProgramExecutionErrorCode {
    graph_program_mismatch,
    entry_block_out_of_bounds,
    host_allocation_failure,
    execution_budget_exhausted,
    block_execution_failure,
};

struct ShaderScalarProgramExecutionError {
    ShaderScalarProgramExecutionErrorCode code =
        ShaderScalarProgramExecutionErrorCode::
            block_execution_failure;
    std::size_t current_block_index = 0;
    std::vector<ShaderScalarBlockExecution> completed_blocks;
    std::optional<ShaderScalarBlockExecutionError> block_error;

    auto operator<=>(
        const ShaderScalarProgramExecutionError&) const = default;
};

using ShaderScalarProgramExecutionResult =
    astraea::core::Result<
        ShaderScalarProgramExecution,
        ShaderScalarProgramExecutionError>;

// Executes a bounded sequence of validated Shader IR basic blocks against
// explicit caller-supplied generic scalar state. The entry block and maximum
// number of block executions are both explicit inputs.
//
// Successful block effects remain applied if a later block fails or the
// execution budget is exhausted. Completed block results are preserved in the
// returned error. This function never provides an unbounded execution mode and
// does not infer Sony/PS5 shader-entry state.
[[nodiscard]] ShaderScalarProgramExecutionResult
execute_shader_scalar_program(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t entry_block_index,
    std::size_t max_block_executions,
    ShaderScalarState& state);

}  // namespace astraea::graphics
