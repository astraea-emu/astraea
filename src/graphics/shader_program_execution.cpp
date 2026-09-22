#include <astraea/graphics/shader_program_execution.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderScalarProgramExecutionError program_error(
    ShaderScalarProgramExecutionErrorCode code,
    std::size_t current_block_index,
    std::vector<ShaderScalarBlockExecution> completed_blocks = {},
    std::optional<ShaderScalarBlockExecutionError> block_error =
        std::nullopt) {
    return ShaderScalarProgramExecutionError{
        .code = code,
        .current_block_index = current_block_index,
        .completed_blocks = std::move(completed_blocks),
        .block_error = std::move(block_error),
    };
}

}  // namespace

ShaderScalarProgramExecutionResult
execute_shader_scalar_program(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t entry_block_index,
    std::size_t max_block_executions,
    ShaderScalarState& state) {
    if (graph.source_word_count !=
            program.source_word_count ||
        graph.emission_count !=
            program.emissions.size()) {
        return ShaderScalarProgramExecutionResult::failure(
            program_error(
                ShaderScalarProgramExecutionErrorCode::
                    graph_program_mismatch,
                entry_block_index));
    }

    if (entry_block_index >= graph.blocks.size()) {
        return ShaderScalarProgramExecutionResult::failure(
            program_error(
                ShaderScalarProgramExecutionErrorCode::
                    entry_block_out_of_bounds,
                entry_block_index));
    }

    std::vector<ShaderScalarBlockExecution>
        block_executions;
    try {
        block_executions.reserve(
            max_block_executions);
    } catch (const std::bad_alloc&) {
        return ShaderScalarProgramExecutionResult::failure(
            program_error(
                ShaderScalarProgramExecutionErrorCode::
                    host_allocation_failure,
                entry_block_index));
    } catch (const std::length_error&) {
        return ShaderScalarProgramExecutionResult::failure(
            program_error(
                ShaderScalarProgramExecutionErrorCode::
                    host_allocation_failure,
                entry_block_index));
    }

    auto current_block_index = entry_block_index;

    while (true) {
        if (block_executions.size() >=
            max_block_executions) {
            return ShaderScalarProgramExecutionResult::failure(
                program_error(
                    ShaderScalarProgramExecutionErrorCode::
                        execution_budget_exhausted,
                    current_block_index,
                    std::move(block_executions)));
        }

        auto block_result =
            execute_shader_scalar_block(
                program,
                graph,
                current_block_index,
                state);
        if (!block_result.has_value()) {
            return ShaderScalarProgramExecutionResult::failure(
                program_error(
                    ShaderScalarProgramExecutionErrorCode::
                        block_execution_failure,
                    current_block_index,
                    std::move(block_executions),
                    block_result.error()));
        }

        auto block_execution =
            std::move(block_result).value();
        const auto successor =
            block_execution.successor.edge;
        block_executions.push_back(
            std::move(block_execution));

        if (!successor.has_value()) {
            return ShaderScalarProgramExecutionResult::success(
                ShaderScalarProgramExecution{
                    .entry_block_index =
                        entry_block_index,
                    .block_executions =
                        std::move(block_executions),
                });
        }

        current_block_index =
            successor->target_block_index;
    }
}

}  // namespace astraea::graphics
