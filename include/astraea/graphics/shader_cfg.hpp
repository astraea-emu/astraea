#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_program.hpp>

namespace astraea::graphics {

enum class ShaderCfgEdgeKind {
    linear_fallthrough,
    unconditional_branch,
    conditional_branch_taken,
    conditional_branch_fallthrough,
};

struct ShaderCfgEdge {
    ShaderCfgEdgeKind kind =
        ShaderCfgEdgeKind::linear_fallthrough;
    std::size_t target_block_index = 0;

    auto operator<=>(const ShaderCfgEdge&) const = default;
};

struct ShaderCfgBasicBlock {
    std::size_t first_emission_index = 0;
    std::size_t emission_count = 0;
    std::vector<ShaderCfgEdge> successors;

    auto operator<=>(const ShaderCfgBasicBlock&) const = default;
};

struct ShaderControlFlowGraph {
    std::size_t source_word_count = 0;
    std::size_t emission_count = 0;
    std::vector<ShaderCfgBasicBlock> blocks;

    auto operator<=>(const ShaderControlFlowGraph&) const = default;
};

enum class ShaderCfgErrorCode {
    invalid_program_layout,
    invalid_branch_delta,
    invalid_branch_target,
    host_allocation_failure,
};

struct ShaderCfgError {
    ShaderCfgErrorCode code =
        ShaderCfgErrorCode::invalid_program_layout;
    std::size_t emission_index = 0;
    std::size_t source_word_index = 0;
    std::optional<std::int64_t> target_word_index;

    auto operator<=>(const ShaderCfgError&) const = default;
};

using ShaderCfgResult =
    astraea::core::Result<
        ShaderControlFlowGraph,
        ShaderCfgError>;

// Builds a structural control-flow graph over an already-lowered bounded
// ShaderIrProgram. Blocks refer to the program's emissions by index; the
// original ordered program remains the source of instruction semantics and
// provenance.
//
// This function validates instruction extents and branch targets, but does not
// evaluate branch conditions, execute waves/lanes, parse Sony shader
// containers, or lower to SPIR-V/Vulkan.
[[nodiscard]] ShaderCfgResult
build_shader_control_flow_graph(
    const ShaderIrProgram& program);

}  // namespace astraea::graphics
