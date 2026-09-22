#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_wave_block_execution.hpp>

namespace astraea::graphics {

struct ShaderWaveProgramExecution {
    std::size_t entry_block_index = 0;
    std::vector<ShaderWaveBlockExecution> block_executions;

    auto operator<=>(const ShaderWaveProgramExecution&) const =
        default;
};

enum class ShaderWaveProgramExecutionErrorCode {
    graph_program_mismatch,
    entry_block_out_of_bounds,
    host_allocation_failure,
    execution_budget_exhausted,
    block_execution_failure,
};

struct ShaderWaveProgramExecutionError {
    ShaderWaveProgramExecutionErrorCode code =
        ShaderWaveProgramExecutionErrorCode::
            block_execution_failure;
    std::size_t current_block_index = 0;
    std::vector<ShaderWaveBlockExecution> completed_blocks;
    std::optional<ShaderWaveBlockExecutionError> block_error;

    auto operator<=>(
        const ShaderWaveProgramExecutionError&) const = default;
};

using ShaderWaveProgramExecutionResult =
    astraea::core::Result<
        ShaderWaveProgramExecution,
        ShaderWaveProgramExecutionError>;

// Executes a bounded sequence of validated mixed scalar/vector Shader IR basic
// blocks against explicit caller-supplied generic wave state. The entry block
// and maximum number of block executions are both explicit inputs.
//
// Successful block effects remain applied if a later block fails or the
// execution budget is exhausted. Completed block results are preserved in the
// returned error. No unbounded mode or PS5 shader-entry state is inferred.
[[nodiscard]] ShaderWaveProgramExecutionResult
execute_shader_wave_program(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t entry_block_index,
    std::size_t max_block_executions,
    ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state);

}  // namespace astraea::graphics
