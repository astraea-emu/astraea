#pragma once

#include <compare>
#include <cstdint>
#include <variant>

#include <astraea/graphics/rdna2_decoder.hpp>

namespace astraea::graphics {

struct ShaderIrNop {
    std::uint8_t repeat_count = 1;

    auto operator<=>(const ShaderIrNop&) const = default;
};

struct ShaderIrEndProgram {
    auto operator<=>(const ShaderIrEndProgram&) const = default;
};

struct ShaderIrRelativeBranch {
    std::int32_t byte_delta = 0;

    auto operator<=>(const ShaderIrRelativeBranch&) const = default;
};

enum class ShaderIrBranchCondition {
    scc_zero,
    scc_one,
    vcc_zero,
    vcc_nonzero,
    exec_zero,
    exec_nonzero,
};

struct ShaderIrConditionalRelativeBranch {
    ShaderIrBranchCondition condition =
        ShaderIrBranchCondition::scc_zero;
    std::int32_t byte_delta = 0;

    auto operator<=>(
        const ShaderIrConditionalRelativeBranch&) const = default;
};

enum class ShaderIrUnsupportedReason {
    unknown_sopp_opcode,
    unsupported_encoding,
    invalid_decoded_instruction,
};

struct ShaderIrUnsupported {
    ShaderIrUnsupportedReason reason =
        ShaderIrUnsupportedReason::unsupported_encoding;

    auto operator<=>(const ShaderIrUnsupported&) const = default;
};

using ShaderIrOperation =
    std::variant<
        ShaderIrNop,
        ShaderIrEndProgram,
        ShaderIrRelativeBranch,
        ShaderIrConditionalRelativeBranch,
        ShaderIrUnsupported>;

struct ShaderIrProvenance {
    Rdna2Instruction source_instruction;

    auto operator<=>(const ShaderIrProvenance&) const = default;
};

// Semantic identity is the Shader IR operation. The decoded instruction,
// source offset, and raw encoding are preserved separately as provenance.
struct ShaderIrEmission {
    ShaderIrOperation operation;
    ShaderIrProvenance provenance;
};

[[nodiscard]] ShaderIrEmission lower_rdna2_to_shader_ir(
    Rdna2Instruction instruction);

[[nodiscard]] bool shader_ir_semantically_equal(
    const ShaderIrEmission& left,
    const ShaderIrEmission& right) noexcept;

}  // namespace astraea::graphics
