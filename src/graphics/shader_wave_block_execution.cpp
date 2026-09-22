#include <astraea/graphics/shader_wave_block_execution.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderWaveBlockExecutionError wave_block_error(
    ShaderWaveBlockExecutionErrorCode code,
    std::size_t block_index,
    std::size_t emission_index,
    std::size_t completed_emission_count,
    std::optional<ShaderScalarExecutionError> scalar_error =
        std::nullopt,
    std::optional<ShaderVectorExecutionError> vector_error =
        std::nullopt,
    std::optional<ShaderCfgSuccessorError> successor_error =
        std::nullopt) noexcept {
    return ShaderWaveBlockExecutionError{
        .code = code,
        .block_index = block_index,
        .emission_index = emission_index,
        .completed_emission_count = completed_emission_count,
        .scalar_error = std::move(scalar_error),
        .vector_error = std::move(vector_error),
        .successor_error = std::move(successor_error),
    };
}

}  // namespace

ShaderWaveBlockExecutionResult
execute_shader_wave_block(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t block_index,
    ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state) {
    if (graph.source_word_count !=
            program.source_word_count ||
        graph.emission_count !=
            program.emissions.size()) {
        return ShaderWaveBlockExecutionResult::failure(
            wave_block_error(
                ShaderWaveBlockExecutionErrorCode::
                    graph_program_mismatch,
                block_index,
                0,
                0));
    }

    if (block_index >= graph.blocks.size()) {
        return ShaderWaveBlockExecutionResult::failure(
            wave_block_error(
                ShaderWaveBlockExecutionErrorCode::
                    block_index_out_of_bounds,
                block_index,
                0,
                0));
    }

    const auto& block = graph.blocks[block_index];
    if (block.emission_count == 0 ||
        block.first_emission_index >
            program.emissions.size() ||
        block.emission_count >
            program.emissions.size() -
                block.first_emission_index) {
        return ShaderWaveBlockExecutionResult::failure(
            wave_block_error(
                ShaderWaveBlockExecutionErrorCode::
                    invalid_block_extent,
                block_index,
                block.first_emission_index,
                0));
    }

    const auto last_emission_index =
        block.first_emission_index +
        block.emission_count - 1U;

    for (std::size_t emission_index =
             block.first_emission_index;
         emission_index < last_emission_index;
         ++emission_index) {
        const auto& operation =
            program.emissions[emission_index].operation;
        if (std::holds_alternative<
                ShaderIrEndProgram>(
                operation) ||
            std::holds_alternative<
                ShaderIrRelativeBranch>(
                operation) ||
            std::holds_alternative<
                ShaderIrConditionalRelativeBranch>(
                operation)) {
            return ShaderWaveBlockExecutionResult::failure(
                wave_block_error(
                    ShaderWaveBlockExecutionErrorCode::
                        invalid_block_control_flow,
                    block_index,
                    emission_index,
                    0));
        }
    }

    std::vector<ShaderWaveExecutionEffect> effects;
    try {
        effects.reserve(block.emission_count);
    } catch (const std::bad_alloc&) {
        return ShaderWaveBlockExecutionResult::failure(
            wave_block_error(
                ShaderWaveBlockExecutionErrorCode::
                    host_allocation_failure,
                block_index,
                block.first_emission_index,
                0));
    } catch (const std::length_error&) {
        return ShaderWaveBlockExecutionResult::failure(
            wave_block_error(
                ShaderWaveBlockExecutionErrorCode::
                    host_allocation_failure,
                block_index,
                block.first_emission_index,
                0));
    }

    std::optional<ShaderBranchDecision> branch_decision;
    std::size_t completed_emission_count = 0;

    for (std::size_t emission_index =
             block.first_emission_index;
         emission_index <= last_emission_index;
         ++emission_index) {
        const auto& operation =
            program.emissions[emission_index].operation;

        if (std::holds_alternative<ShaderIrNop>(
                operation) ||
            std::holds_alternative<ShaderIrRelativeBranch>(
                operation) ||
            std::holds_alternative<ShaderIrEndProgram>(
                operation)) {
            ++completed_emission_count;
            continue;
        }

        if (const auto* conditional =
                std::get_if<
                    ShaderIrConditionalRelativeBranch>(
                    &operation);
            conditional != nullptr) {
            branch_decision =
                evaluate_shader_branch_condition(
                    conditional->condition,
                    scalar_state);
            ++completed_emission_count;
            continue;
        }

        if (std::holds_alternative<
                ShaderIrScalarMove32>(
                operation) ||
            std::holds_alternative<
                ShaderIrScalarMove64>(
                operation)) {
            auto scalar_result =
                execute_shader_scalar_operation(
                    operation,
                    scalar_state);
            if (!scalar_result.has_value()) {
                return ShaderWaveBlockExecutionResult::failure(
                    wave_block_error(
                        ShaderWaveBlockExecutionErrorCode::
                            scalar_execution_failure,
                        block_index,
                        emission_index,
                        completed_emission_count,
                        scalar_result.error()));
            }

            effects.emplace_back(
                std::move(scalar_result).value());
            ++completed_emission_count;
            continue;
        }

        if (std::holds_alternative<
                ShaderIrVectorMove32>(
                operation)) {
            auto vector_result =
                execute_shader_vector_move32_operation(
                    operation,
                    scalar_state,
                    vector_state);
            if (!vector_result.has_value()) {
                return ShaderWaveBlockExecutionResult::failure(
                    wave_block_error(
                        ShaderWaveBlockExecutionErrorCode::
                            vector_execution_failure,
                        block_index,
                        emission_index,
                        completed_emission_count,
                        std::nullopt,
                        vector_result.error()));
            }

            effects.emplace_back(
                std::move(vector_result).value());
            ++completed_emission_count;
            continue;
        }

        if (std::holds_alternative<
                ShaderIrVectorAddF32>(
                operation)) {
            auto vector_result =
                execute_shader_vector_add_f32_exact_operation(
                    operation,
                    scalar_state,
                    vector_state);
            if (!vector_result.has_value()) {
                return ShaderWaveBlockExecutionResult::failure(
                    wave_block_error(
                        ShaderWaveBlockExecutionErrorCode::
                            vector_execution_failure,
                        block_index,
                        emission_index,
                        completed_emission_count,
                        std::nullopt,
                        vector_result.error()));
            }

            effects.emplace_back(
                std::move(vector_result).value());
            ++completed_emission_count;
            continue;
        }

        return ShaderWaveBlockExecutionResult::failure(
            wave_block_error(
                ShaderWaveBlockExecutionErrorCode::
                    unsupported_operation,
                block_index,
                emission_index,
                completed_emission_count));
    }

    auto successor =
        select_shader_cfg_successor(
            program,
            graph,
            block_index,
            branch_decision);
    if (!successor.has_value()) {
        return ShaderWaveBlockExecutionResult::failure(
            wave_block_error(
                ShaderWaveBlockExecutionErrorCode::
                    cfg_successor_failure,
                block_index,
                last_emission_index,
                completed_emission_count,
                std::nullopt,
                std::nullopt,
                successor.error()));
    }

    return ShaderWaveBlockExecutionResult::success(
        ShaderWaveBlockExecution{
            .block_index = block_index,
            .executed_emission_count =
                completed_emission_count,
            .effects = std::move(effects),
            .branch_decision = branch_decision,
            .successor =
                std::move(successor).value(),
        });
}

}  // namespace astraea::graphics
