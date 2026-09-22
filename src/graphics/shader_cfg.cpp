#include <astraea/graphics/shader_cfg.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace astraea::graphics {
namespace {

struct BranchTargetResolution {
    bool valid = false;
    ShaderCfgErrorCode error_code =
        ShaderCfgErrorCode::invalid_branch_target;
    std::optional<std::int64_t> target_word_index;
    std::size_t target_emission_index = 0;
};

[[nodiscard]] ShaderCfgError error(
    ShaderCfgErrorCode code,
    std::size_t emission_index,
    std::size_t source_word_index,
    std::optional<std::int64_t> target_word_index =
        std::nullopt) noexcept {
    return ShaderCfgError{
        .code = code,
        .emission_index = emission_index,
        .source_word_index = source_word_index,
        .target_word_index = target_word_index,
    };
}

[[nodiscard]] std::optional<ShaderCfgError>
validate_program_layout(
    const ShaderIrProgram& program) noexcept {
    constexpr auto kMaxSignedWordIndex =
        static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max());

    if (program.source_word_count >
        kMaxSignedWordIndex) {
        return error(
            ShaderCfgErrorCode::invalid_program_layout,
            0,
            0);
    }

    std::size_t expected_word_index = 0;
    for (std::size_t emission_index = 0;
         emission_index < program.emissions.size();
         ++emission_index) {
        const auto& instruction =
            program.emissions[emission_index]
                .provenance.source_instruction;

        if (instruction.word_count == 0 ||
            instruction.word_index != expected_word_index ||
            expected_word_index > program.source_word_count ||
            instruction.word_count >
                program.source_word_count -
                    expected_word_index ||
            expected_word_index >
                std::numeric_limits<std::size_t>::max() /
                    4U ||
            instruction.byte_offset !=
                expected_word_index * 4U) {
            return error(
                ShaderCfgErrorCode::invalid_program_layout,
                emission_index,
                instruction.word_index);
        }

        expected_word_index += instruction.word_count;
    }

    if (expected_word_index !=
        program.source_word_count) {
        return error(
            ShaderCfgErrorCode::invalid_program_layout,
            program.emissions.size(),
            expected_word_index);
    }

    return std::nullopt;
}

[[nodiscard]] BranchTargetResolution
resolve_branch_target(
    const ShaderIrProgram& program,
    std::size_t emission_index,
    std::int32_t byte_delta) noexcept {
    const auto& instruction =
        program.emissions[emission_index]
            .provenance.source_instruction;

    if ((byte_delta % 4) != 0) {
        return BranchTargetResolution{
            .valid = false,
            .error_code =
                ShaderCfgErrorCode::invalid_branch_delta,
            .target_word_index = std::nullopt,
            .target_emission_index = 0,
        };
    }

    const auto source_word_index =
        static_cast<std::int64_t>(
            instruction.word_index);
    const auto delta_words =
        static_cast<std::int64_t>(
            byte_delta / 4);

    if (delta_words > 0 &&
        source_word_index >
            std::numeric_limits<std::int64_t>::max() -
                delta_words) {
        return BranchTargetResolution{
            .valid = false,
            .error_code =
                ShaderCfgErrorCode::invalid_branch_target,
            .target_word_index = std::nullopt,
            .target_emission_index = 0,
        };
    }

    const auto target_word_index =
        source_word_index + delta_words;

    if (target_word_index < 0 ||
        target_word_index >=
            static_cast<std::int64_t>(
                program.source_word_count)) {
        return BranchTargetResolution{
            .valid = false,
            .error_code =
                ShaderCfgErrorCode::invalid_branch_target,
            .target_word_index = target_word_index,
            .target_emission_index = 0,
        };
    }

    const auto target =
        static_cast<std::size_t>(
            target_word_index);
    const auto found =
        std::lower_bound(
            program.emissions.begin(),
            program.emissions.end(),
            target,
            [](
                const ShaderIrEmission& emission,
                std::size_t word_index) {
                return emission.provenance
                           .source_instruction.word_index <
                       word_index;
            });

    if (found == program.emissions.end() ||
        found->provenance.source_instruction.word_index !=
            target) {
        return BranchTargetResolution{
            .valid = false,
            .error_code =
                ShaderCfgErrorCode::invalid_branch_target,
            .target_word_index = target_word_index,
            .target_emission_index = 0,
        };
    }

    return BranchTargetResolution{
        .valid = true,
        .error_code =
            ShaderCfgErrorCode::invalid_branch_target,
        .target_word_index = target_word_index,
        .target_emission_index =
            static_cast<std::size_t>(
                found - program.emissions.begin()),
    };
}

}  // namespace

ShaderCfgResult
build_shader_control_flow_graph(
    const ShaderIrProgram& program) {
    if (const auto layout_error =
            validate_program_layout(program);
        layout_error.has_value()) {
        return ShaderCfgResult::failure(
            layout_error.value());
    }

    if (program.emissions.empty()) {
        return ShaderCfgResult::success(
            ShaderControlFlowGraph{
                .source_word_count =
                    program.source_word_count,
                .emission_count = 0,
                .blocks = {},
            });
    }

    try {
        const auto emission_count =
            program.emissions.size();

        std::vector<bool> leaders(
            emission_count,
            false);
        leaders[0] = true;

        std::vector<std::optional<std::size_t>>
            branch_targets(
                emission_count,
                std::nullopt);

        for (std::size_t emission_index = 0;
             emission_index < emission_count;
             ++emission_index) {
            const auto& emission =
                program.emissions[emission_index];
            std::optional<std::int32_t> byte_delta;

            if (const auto* relative_branch =
                    std::get_if<ShaderIrRelativeBranch>(
                        &emission.operation);
                relative_branch != nullptr) {
                byte_delta =
                    relative_branch->byte_delta;
            } else if (
                const auto* conditional_branch =
                    std::get_if<
                        ShaderIrConditionalRelativeBranch>(
                        &emission.operation);
                conditional_branch != nullptr) {
                byte_delta =
                    conditional_branch->byte_delta;
            }

            if (byte_delta.has_value()) {
                const auto resolved =
                    resolve_branch_target(
                        program,
                        emission_index,
                        byte_delta.value());
                if (!resolved.valid) {
                    return ShaderCfgResult::failure(
                        error(
                            resolved.error_code,
                            emission_index,
                            emission.provenance
                                .source_instruction
                                .word_index,
                            resolved.target_word_index));
                }

                branch_targets[emission_index] =
                    resolved.target_emission_index;
                leaders[
                    resolved.target_emission_index] =
                    true;

                if (emission_index + 1U <
                    emission_count) {
                    leaders[emission_index + 1U] =
                        true;
                }
            } else if (
                std::holds_alternative<
                    ShaderIrEndProgram>(
                    emission.operation) &&
                emission_index + 1U <
                    emission_count) {
                leaders[emission_index + 1U] =
                    true;
            }
        }

        std::vector<std::size_t> block_starts;
        block_starts.reserve(emission_count);
        for (std::size_t emission_index = 0;
             emission_index < emission_count;
             ++emission_index) {
            if (leaders[emission_index]) {
                block_starts.push_back(
                    emission_index);
            }
        }

        std::vector<ShaderCfgBasicBlock> blocks;
        blocks.reserve(block_starts.size());

        std::vector<std::size_t>
            emission_to_block(
                emission_count,
                0);

        for (std::size_t block_index = 0;
             block_index < block_starts.size();
             ++block_index) {
            const auto first =
                block_starts[block_index];
            const auto end =
                block_index + 1U <
                        block_starts.size()
                    ? block_starts[
                          block_index + 1U]
                    : emission_count;

            for (auto emission_index = first;
                 emission_index < end;
                 ++emission_index) {
                emission_to_block[
                    emission_index] =
                    block_index;
            }

            blocks.push_back(
                ShaderCfgBasicBlock{
                    .first_emission_index = first,
                    .emission_count = end - first,
                    .successors = {},
                });
        }

        for (std::size_t block_index = 0;
             block_index < blocks.size();
             ++block_index) {
            auto& block = blocks[block_index];
            const auto last_emission_index =
                block.first_emission_index +
                block.emission_count - 1U;
            const auto& operation =
                program.emissions[
                    last_emission_index]
                    .operation;

            if (std::holds_alternative<
                    ShaderIrRelativeBranch>(
                    operation)) {
                block.successors.push_back(
                    ShaderCfgEdge{
                        .kind =
                            ShaderCfgEdgeKind::
                                unconditional_branch,
                        .target_block_index =
                            emission_to_block[
                                branch_targets[
                                    last_emission_index]
                                    .value()],
                    });
                continue;
            }

            if (std::holds_alternative<
                    ShaderIrConditionalRelativeBranch>(
                    operation)) {
                block.successors.push_back(
                    ShaderCfgEdge{
                        .kind =
                            ShaderCfgEdgeKind::
                                conditional_branch_taken,
                        .target_block_index =
                            emission_to_block[
                                branch_targets[
                                    last_emission_index]
                                    .value()],
                    });

                if (last_emission_index + 1U <
                    emission_count) {
                    block.successors.push_back(
                        ShaderCfgEdge{
                            .kind =
                                ShaderCfgEdgeKind::
                                    conditional_branch_fallthrough,
                            .target_block_index =
                                emission_to_block[
                                    last_emission_index +
                                    1U],
                        });
                }
                continue;
            }

            if (std::holds_alternative<
                    ShaderIrEndProgram>(
                    operation)) {
                continue;
            }

            if (last_emission_index + 1U <
                emission_count) {
                block.successors.push_back(
                    ShaderCfgEdge{
                        .kind =
                            ShaderCfgEdgeKind::
                                linear_fallthrough,
                        .target_block_index =
                            emission_to_block[
                                last_emission_index +
                                1U],
                    });
            }
        }

        return ShaderCfgResult::success(
            ShaderControlFlowGraph{
                .source_word_count =
                    program.source_word_count,
                .emission_count =
                    emission_count,
                .blocks = std::move(blocks),
            });
    } catch (const std::bad_alloc&) {
        return ShaderCfgResult::failure(
            error(
                ShaderCfgErrorCode::
                    host_allocation_failure,
                0,
                0));
    } catch (const std::length_error&) {
        return ShaderCfgResult::failure(
            error(
                ShaderCfgErrorCode::
                    host_allocation_failure,
                0,
                0));
    }
}

}  // namespace astraea::graphics
