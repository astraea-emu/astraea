#pragma once

#include <array>
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

struct ShaderIrWorkgroupBarrier {
    auto operator<=>(
        const ShaderIrWorkgroupBarrier&) const = default;
};

struct ShaderIrWaitCount {
    std::uint8_t vmcnt = 0;
    std::uint8_t expcnt = 0;
    std::uint8_t lgkmcnt = 0;

    auto operator<=>(const ShaderIrWaitCount&) const = default;
};

struct ShaderIrVgpr {
    std::uint8_t index = 0;

    auto operator<=>(const ShaderIrVgpr&) const = default;
};

struct ShaderIrVectorMove32 {
    ShaderIrVgpr destination;
    ShaderIrVgpr source;

    auto operator<=>(const ShaderIrVectorMove32&) const = default;
};

struct ShaderIrVectorAddF32 {
    ShaderIrVgpr destination;
    ShaderIrVgpr source0;
    ShaderIrVgpr source1;

    auto operator<=>(const ShaderIrVectorAddF32&) const = default;
};

enum class ShaderIrExportTargetKind {
    mrt,
    mrt_z,
    null_target,
    position,
    primitive,
    parameter,
};

struct ShaderIrExport {
    ShaderIrExportTargetKind target_kind =
        ShaderIrExportTargetKind::mrt;
    std::uint8_t target_index = 0;
    std::uint8_t enable_mask = 0;
    bool compressed = false;
    bool done = false;
    bool valid_mask = false;
    std::array<ShaderIrVgpr, 4> sources{};

    auto operator<=>(const ShaderIrExport&) const = default;
};

struct ShaderIrSgpr {
    std::uint8_t index = 0;

    auto operator<=>(const ShaderIrSgpr&) const = default;
};

enum class ShaderIrSpecialScalarSourceKind32 {
    vcc_lo,
    vcc_hi,
    m0,
    null_register,
    exec_lo,
    exec_hi,
};

struct ShaderIrSpecialScalarSource32 {
    ShaderIrSpecialScalarSourceKind32 kind =
        ShaderIrSpecialScalarSourceKind32::vcc_lo;

    auto operator<=>(
        const ShaderIrSpecialScalarSource32&) const = default;
};

struct ShaderIrInlineInteger32 {
    std::int32_t value = 0;

    auto operator<=>(const ShaderIrInlineInteger32&) const = default;
};

struct ShaderIrLiteral32 {
    std::uint32_t bits = 0;

    auto operator<=>(const ShaderIrLiteral32&) const = default;
};

using ShaderIrScalarSource32 =
    std::variant<
        ShaderIrSgpr,
        ShaderIrSpecialScalarSource32,
        ShaderIrInlineInteger32,
        ShaderIrLiteral32>;

struct ShaderIrScalarMove32 {
    ShaderIrSgpr destination;
    ShaderIrScalarSource32 source;

    auto operator<=>(const ShaderIrScalarMove32&) const = default;
};

struct ShaderIrSgprPair {
    std::uint8_t first_index = 0;

    auto operator<=>(const ShaderIrSgprPair&) const = default;
};

struct ShaderIrScalarMove64 {
    ShaderIrSgprPair destination;
    ShaderIrSgprPair source;

    auto operator<=>(const ShaderIrScalarMove64&) const = default;
};

enum class ShaderIrUnsupportedReason {
    unknown_sopp_opcode,
    unknown_sop1_opcode,
    unknown_vop1_opcode,
    unknown_vop2_opcode,
    unknown_export_target,
    unsupported_scalar_operand,
    unsupported_vector_operand,
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
        ShaderIrWorkgroupBarrier,
        ShaderIrWaitCount,
        ShaderIrScalarMove32,
        ShaderIrScalarMove64,
        ShaderIrVectorMove32,
        ShaderIrVectorAddF32,
        ShaderIrExport,
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
