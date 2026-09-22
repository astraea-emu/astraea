#include <astraea/graphics/shader_ir.hpp>

#include <cstdint>
#include <utility>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderIrUnsupported unsupported(
    ShaderIrUnsupportedReason reason) noexcept {
    return ShaderIrUnsupported{
        .reason = reason,
    };
}

[[nodiscard]] bool valid_sopp_source(
    const Rdna2Instruction& instruction) noexcept {
    return instruction.format ==
               Rdna2InstructionFormat::sopp &&
           instruction.sopp.has_value() &&
           !instruction.sop1.has_value();
}

[[nodiscard]] bool valid_sop1_source(
    const Rdna2Instruction& instruction) noexcept {
    return instruction.format ==
               Rdna2InstructionFormat::sop1 &&
           instruction.sop1.has_value() &&
           !instruction.sopp.has_value();
}

[[nodiscard]] bool plain_sgpr_selector(
    std::uint8_t selector) noexcept {
    return selector <= 105U;
}

[[nodiscard]] std::int32_t relative_branch_delta(
    const Rdna2Instruction& instruction) noexcept {
    return static_cast<std::int32_t>(
               instruction.sopp->simm16) *
               4 +
           4;
}

[[nodiscard]] ShaderIrWaitCount wait_count(
    const Rdna2Instruction& instruction) noexcept {
    const auto bits =
        static_cast<std::uint16_t>(
            instruction.sopp->simm16);
    const auto vmcnt =
        static_cast<std::uint8_t>(
            (((bits >> 14U) & 0x3U) << 4U) |
            (bits & 0x0fU));
    const auto expcnt =
        static_cast<std::uint8_t>(
            (bits >> 4U) & 0x7U);
    const auto lgkmcnt =
        static_cast<std::uint8_t>(
            (bits >> 8U) & 0x3fU);

    return ShaderIrWaitCount{
        .vmcnt = vmcnt,
        .expcnt = expcnt,
        .lgkmcnt = lgkmcnt,
    };
}

}  // namespace

ShaderIrEmission lower_rdna2_to_shader_ir(
    Rdna2Instruction instruction) {
    ShaderIrOperation operation =
        unsupported(
            ShaderIrUnsupportedReason::
                invalid_decoded_instruction);

    switch (instruction.kind) {
    case Rdna2InstructionKind::s_nop:
        if (valid_sopp_source(instruction)) {
            const auto immediate_bits =
                static_cast<std::uint16_t>(
                    instruction.sopp->simm16);
            const auto repeat_count =
                static_cast<std::uint8_t>(
                    (immediate_bits & 0x0fU) + 1U);
            operation =
                ShaderIrNop{
                    .repeat_count = repeat_count,
                };
        }
        break;

    case Rdna2InstructionKind::s_endpgm:
        if (valid_sopp_source(instruction)) {
            operation = ShaderIrEndProgram{};
        }
        break;

    case Rdna2InstructionKind::s_branch:
        if (valid_sopp_source(instruction)) {
            operation =
                ShaderIrRelativeBranch{
                    .byte_delta =
                        relative_branch_delta(
                            instruction),
                };
        }
        break;

    case Rdna2InstructionKind::s_cbranch_scc0:
        if (valid_sopp_source(instruction)) {
            operation =
                ShaderIrConditionalRelativeBranch{
                    .condition =
                        ShaderIrBranchCondition::scc_zero,
                    .byte_delta =
                        relative_branch_delta(instruction),
                };
        }
        break;

    case Rdna2InstructionKind::s_cbranch_scc1:
        if (valid_sopp_source(instruction)) {
            operation =
                ShaderIrConditionalRelativeBranch{
                    .condition =
                        ShaderIrBranchCondition::scc_one,
                    .byte_delta =
                        relative_branch_delta(instruction),
                };
        }
        break;

    case Rdna2InstructionKind::s_cbranch_vccz:
        if (valid_sopp_source(instruction)) {
            operation =
                ShaderIrConditionalRelativeBranch{
                    .condition =
                        ShaderIrBranchCondition::vcc_zero,
                    .byte_delta =
                        relative_branch_delta(instruction),
                };
        }
        break;

    case Rdna2InstructionKind::s_cbranch_vccnz:
        if (valid_sopp_source(instruction)) {
            operation =
                ShaderIrConditionalRelativeBranch{
                    .condition =
                        ShaderIrBranchCondition::vcc_nonzero,
                    .byte_delta =
                        relative_branch_delta(instruction),
                };
        }
        break;

    case Rdna2InstructionKind::s_cbranch_execz:
        if (valid_sopp_source(instruction)) {
            operation =
                ShaderIrConditionalRelativeBranch{
                    .condition =
                        ShaderIrBranchCondition::exec_zero,
                    .byte_delta =
                        relative_branch_delta(instruction),
                };
        }
        break;

    case Rdna2InstructionKind::s_cbranch_execnz:
        if (valid_sopp_source(instruction)) {
            operation =
                ShaderIrConditionalRelativeBranch{
                    .condition =
                        ShaderIrBranchCondition::exec_nonzero,
                    .byte_delta =
                        relative_branch_delta(instruction),
                };
        }
        break;

    case Rdna2InstructionKind::s_barrier:
        if (valid_sopp_source(instruction)) {
            operation = ShaderIrWorkgroupBarrier{};
        }
        break;

    case Rdna2InstructionKind::s_waitcnt:
        if (valid_sopp_source(instruction)) {
            operation = wait_count(instruction);
        }
        break;

    case Rdna2InstructionKind::s_mov_b32:
        if (valid_sop1_source(instruction)) {
            const auto destination =
                instruction.sop1->destination_selector;
            const auto source =
                instruction.sop1->source_selector;
            if (plain_sgpr_selector(destination) &&
                plain_sgpr_selector(source)) {
                operation =
                    ShaderIrScalarMove32{
                        .destination =
                            ShaderIrSgpr{
                                .index = destination,
                            },
                        .source =
                            ShaderIrSgpr{
                                .index = source,
                            },
                    };
            } else {
                operation =
                    unsupported(
                        ShaderIrUnsupportedReason::
                            unsupported_scalar_operand);
            }
        }
        break;

    case Rdna2InstructionKind::unknown_sopp_opcode:
        if (valid_sopp_source(instruction)) {
            operation =
                unsupported(
                    ShaderIrUnsupportedReason::
                        unknown_sopp_opcode);
        }
        break;

    case Rdna2InstructionKind::unknown_sop1_opcode:
        if (valid_sop1_source(instruction)) {
            operation =
                unsupported(
                    ShaderIrUnsupportedReason::
                        unknown_sop1_opcode);
        }
        break;

    case Rdna2InstructionKind::unsupported_encoding:
        if (instruction.format ==
                Rdna2InstructionFormat::unsupported &&
            !instruction.sopp.has_value()) {
            operation =
                unsupported(
                    ShaderIrUnsupportedReason::
                        unsupported_encoding);
        }
        break;
    }

    return ShaderIrEmission{
        .operation = std::move(operation),
        .provenance =
            ShaderIrProvenance{
                .source_instruction =
                    std::move(instruction),
            },
    };
}

bool shader_ir_semantically_equal(
    const ShaderIrEmission& left,
    const ShaderIrEmission& right) noexcept {
    return left.operation == right.operation;
}

}  // namespace astraea::graphics
