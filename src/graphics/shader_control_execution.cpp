#include <astraea/graphics/shader_control_execution.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderCfgSuccessorError successor_error(
    ShaderCfgSuccessorErrorCode code,
    std::size_t block_index,
    std::optional<std::size_t> edge_index =
        std::nullopt) noexcept {
    return ShaderCfgSuccessorError{
        .code = code,
        .block_index = block_index,
        .edge_index = edge_index,
    };
}

[[nodiscard]] bool valid_target(
    const ShaderControlFlowGraph& graph,
    const ShaderCfgEdge& edge) noexcept {
    return edge.target_block_index < graph.blocks.size();
}

[[nodiscard]] ShaderScalarBlockExecutionError block_error(
    ShaderScalarBlockExecutionErrorCode code,
    std::size_t block_index,
    std::size_t emission_index,
    std::size_t completed_emission_count,
    std::optional<ShaderScalarExecutionError> scalar_error =
        std::nullopt,
    std::optional<ShaderCfgSuccessorError> successor_error =
        std::nullopt) noexcept {
    return ShaderScalarBlockExecutionError{
        .code = code,
        .block_index = block_index,
        .emission_index = emission_index,
        .completed_emission_count = completed_emission_count,
        .scalar_error = std::move(scalar_error),
        .successor_error = std::move(successor_error),
    };
}

}  // namespace

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

ShaderCfgSuccessorResult
select_shader_cfg_successor(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t block_index,
    std::optional<ShaderBranchDecision> branch_decision) noexcept {
    if (graph.source_word_count !=
            program.source_word_count ||
        graph.emission_count !=
            program.emissions.size()) {
        return ShaderCfgSuccessorResult::failure(
            successor_error(
                ShaderCfgSuccessorErrorCode::
                    graph_program_mismatch,
                block_index));
    }

    if (block_index >= graph.blocks.size()) {
        return ShaderCfgSuccessorResult::failure(
            successor_error(
                ShaderCfgSuccessorErrorCode::
                    block_index_out_of_bounds,
                block_index));
    }

    const auto& block = graph.blocks[block_index];
    if (block.emission_count == 0 ||
        block.first_emission_index >
            program.emissions.size() ||
        block.emission_count >
            program.emissions.size() -
                block.first_emission_index) {
        return ShaderCfgSuccessorResult::failure(
            successor_error(
                ShaderCfgSuccessorErrorCode::
                    invalid_block_extent,
                block_index));
    }

    for (std::size_t edge_index = 0;
         edge_index < block.successors.size();
         ++edge_index) {
        if (!valid_target(
                graph,
                block.successors[edge_index])) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        invalid_edge_target,
                    block_index,
                    edge_index));
        }
    }

    const auto last_emission_index =
        block.first_emission_index +
        block.emission_count - 1U;
    const auto& operation =
        program.emissions[last_emission_index].operation;
    const auto has_source_fallthrough =
        last_emission_index + 1U <
        program.emissions.size();

    if (std::holds_alternative<
            ShaderIrEndProgram>(
            operation)) {
        if (branch_decision.has_value()) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        unexpected_branch_decision,
                    block_index));
        }
        if (!block.successors.empty()) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        invalid_successor_topology,
                    block_index));
        }

        return ShaderCfgSuccessorResult::success(
            ShaderCfgSuccessorSelection{
                .edge = std::nullopt,
            });
    }

    if (std::holds_alternative<
            ShaderIrRelativeBranch>(
            operation)) {
        if (branch_decision.has_value()) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        unexpected_branch_decision,
                    block_index));
        }
        if (block.successors.size() != 1 ||
            block.successors[0].kind !=
                ShaderCfgEdgeKind::
                    unconditional_branch) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        invalid_successor_topology,
                    block_index));
        }

        return ShaderCfgSuccessorResult::success(
            ShaderCfgSuccessorSelection{
                .edge = block.successors[0],
            });
    }

    if (const auto* conditional =
            std::get_if<
                ShaderIrConditionalRelativeBranch>(
                &operation);
        conditional != nullptr) {
        if (!branch_decision.has_value()) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        branch_decision_required,
                    block_index));
        }
        if (branch_decision->condition !=
            conditional->condition) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        branch_condition_mismatch,
                    block_index));
        }

        const ShaderCfgEdge* taken_edge = nullptr;
        const ShaderCfgEdge* fallthrough_edge = nullptr;
        for (const auto& edge : block.successors) {
            switch (edge.kind) {
            case ShaderCfgEdgeKind::
                conditional_branch_taken:
                if (taken_edge != nullptr) {
                    return ShaderCfgSuccessorResult::failure(
                        successor_error(
                            ShaderCfgSuccessorErrorCode::
                                invalid_successor_topology,
                            block_index));
                }
                taken_edge = &edge;
                break;
            case ShaderCfgEdgeKind::
                conditional_branch_fallthrough:
                if (fallthrough_edge != nullptr) {
                    return ShaderCfgSuccessorResult::failure(
                        successor_error(
                            ShaderCfgSuccessorErrorCode::
                                invalid_successor_topology,
                            block_index));
                }
                fallthrough_edge = &edge;
                break;
            case ShaderCfgEdgeKind::linear_fallthrough:
            case ShaderCfgEdgeKind::unconditional_branch:
                return ShaderCfgSuccessorResult::failure(
                    successor_error(
                        ShaderCfgSuccessorErrorCode::
                            invalid_successor_topology,
                        block_index));
            }
        }

        if (taken_edge == nullptr ||
            (has_source_fallthrough &&
             fallthrough_edge == nullptr) ||
            (!has_source_fallthrough &&
             fallthrough_edge != nullptr) ||
            block.successors.size() !=
                (has_source_fallthrough ? 2U : 1U)) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        invalid_successor_topology,
                    block_index));
        }

        if (has_source_fallthrough &&
            fallthrough_edge->target_block_index !=
                block_index + 1U) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        invalid_successor_topology,
                    block_index));
        }

        if (branch_decision->taken) {
            return ShaderCfgSuccessorResult::success(
                ShaderCfgSuccessorSelection{
                    .edge = *taken_edge,
                });
        }

        if (fallthrough_edge != nullptr) {
            return ShaderCfgSuccessorResult::success(
                ShaderCfgSuccessorSelection{
                    .edge = *fallthrough_edge,
                });
        }

        return ShaderCfgSuccessorResult::success(
            ShaderCfgSuccessorSelection{
                .edge = std::nullopt,
            });
    }

    if (branch_decision.has_value()) {
        return ShaderCfgSuccessorResult::failure(
            successor_error(
                ShaderCfgSuccessorErrorCode::
                    unexpected_branch_decision,
                block_index));
    }

    if (has_source_fallthrough) {
        if (block.successors.size() != 1 ||
            block.successors[0].kind !=
                ShaderCfgEdgeKind::
                    linear_fallthrough ||
            block.successors[0].target_block_index !=
                block_index + 1U) {
            return ShaderCfgSuccessorResult::failure(
                successor_error(
                    ShaderCfgSuccessorErrorCode::
                        invalid_successor_topology,
                    block_index));
        }

        return ShaderCfgSuccessorResult::success(
            ShaderCfgSuccessorSelection{
                .edge = block.successors[0],
            });
    }

    if (!block.successors.empty()) {
        return ShaderCfgSuccessorResult::failure(
            successor_error(
                ShaderCfgSuccessorErrorCode::
                    invalid_successor_topology,
                block_index));
    }

    return ShaderCfgSuccessorResult::success(
        ShaderCfgSuccessorSelection{
            .edge = std::nullopt,
        });
}

ShaderScalarBlockExecutionResult
execute_shader_scalar_block(
    const ShaderIrProgram& program,
    const ShaderControlFlowGraph& graph,
    std::size_t block_index,
    ShaderScalarState& state) {
    if (graph.source_word_count !=
            program.source_word_count ||
        graph.emission_count !=
            program.emissions.size()) {
        return ShaderScalarBlockExecutionResult::failure(
            block_error(
                ShaderScalarBlockExecutionErrorCode::
                    graph_program_mismatch,
                block_index,
                0,
                0));
    }

    if (block_index >= graph.blocks.size()) {
        return ShaderScalarBlockExecutionResult::failure(
            block_error(
                ShaderScalarBlockExecutionErrorCode::
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
        return ShaderScalarBlockExecutionResult::failure(
            block_error(
                ShaderScalarBlockExecutionErrorCode::
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
            return ShaderScalarBlockExecutionResult::failure(
                block_error(
                    ShaderScalarBlockExecutionErrorCode::
                        invalid_block_control_flow,
                    block_index,
                    emission_index,
                    0));
        }
    }

    std::vector<ShaderScalarExecutionEffect>
        scalar_write_effects;
    try {
        scalar_write_effects.reserve(
            block.emission_count);
    } catch (const std::bad_alloc&) {
        return ShaderScalarBlockExecutionResult::failure(
            block_error(
                ShaderScalarBlockExecutionErrorCode::
                    host_allocation_failure,
                block_index,
                block.first_emission_index,
                0));
    } catch (const std::length_error&) {
        return ShaderScalarBlockExecutionResult::failure(
            block_error(
                ShaderScalarBlockExecutionErrorCode::
                    host_allocation_failure,
                block_index,
                block.first_emission_index,
                0));
    }

    std::optional<ShaderBranchDecision>
        branch_decision;
    std::size_t completed_emission_count = 0;

    for (std::size_t emission_index =
             block.first_emission_index;
         emission_index <= last_emission_index;
         ++emission_index) {
        const auto& operation =
            program.emissions[emission_index].operation;

        if (std::holds_alternative<
                ShaderIrNop>(
                operation) ||
            std::holds_alternative<
                ShaderIrRelativeBranch>(
                operation) ||
            std::holds_alternative<
                ShaderIrEndProgram>(
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
                    state);
            ++completed_emission_count;
            continue;
        }

        if (std::holds_alternative<
                ShaderIrScalarMove32>(
                operation) ||
            std::holds_alternative<
                ShaderIrScalarMove64>(
                operation)) {
            auto execution =
                execute_shader_scalar_operation(
                    operation,
                    state);
            if (!execution.has_value()) {
                return ShaderScalarBlockExecutionResult::failure(
                    block_error(
                        ShaderScalarBlockExecutionErrorCode::
                            scalar_execution_failure,
                        block_index,
                        emission_index,
                        completed_emission_count,
                        execution.error()));
            }

            scalar_write_effects.push_back(
                std::move(execution).value());
            ++completed_emission_count;
            continue;
        }

        return ShaderScalarBlockExecutionResult::failure(
            block_error(
                ShaderScalarBlockExecutionErrorCode::
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
        return ShaderScalarBlockExecutionResult::failure(
            block_error(
                ShaderScalarBlockExecutionErrorCode::
                    cfg_successor_failure,
                block_index,
                last_emission_index,
                completed_emission_count,
                std::nullopt,
                successor.error()));
    }

    return ShaderScalarBlockExecutionResult::success(
        ShaderScalarBlockExecution{
            .block_index = block_index,
            .executed_emission_count =
                completed_emission_count,
            .scalar_write_effects =
                std::move(scalar_write_effects),
            .branch_decision =
                std::move(branch_decision),
            .successor =
                std::move(successor).value(),
        });
}

}  // namespace astraea::graphics
