#include <astraea/graphics/shader_ir.hpp>

#include <cstdint>
#include <optional>
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
           !instruction.sop1.has_value() &&
           !instruction.vop1.has_value() &&
           !instruction.vop2.has_value() &&
           !instruction.exp.has_value();
}

[[nodiscard]] bool valid_vop1_source(
    const Rdna2Instruction& instruction) noexcept {
    return instruction.format ==
               Rdna2InstructionFormat::vop1 &&
           instruction.vop1.has_value() &&
           !instruction.sopp.has_value() &&
           !instruction.sop1.has_value() &&
           !instruction.vop2.has_value() &&
           !instruction.exp.has_value();
}

[[nodiscard]] bool valid_vop2_source(
    const Rdna2Instruction& instruction) noexcept {
    return instruction.format ==
               Rdna2InstructionFormat::vop2 &&
           instruction.vop2.has_value() &&
           !instruction.sopp.has_value() &&
           !instruction.sop1.has_value() &&
           !instruction.vop1.has_value() &&
           !instruction.exp.has_value();
}

[[nodiscard]] bool valid_sop1_source(
    const Rdna2Instruction& instruction) noexcept {
    return instruction.format ==
               Rdna2InstructionFormat::sop1 &&
           instruction.sop1.has_value() &&
           !instruction.sopp.has_value() &&
           !instruction.vop1.has_value() &&
           !instruction.vop2.has_value() &&
           !instruction.exp.has_value();
}

[[nodiscard]] bool valid_exp_source(
    const Rdna2Instruction& instruction) noexcept {
    return instruction.format ==
               Rdna2InstructionFormat::exp &&
           instruction.exp.has_value() &&
           !instruction.sopp.has_value() &&
           !instruction.sop1.has_value() &&
           !instruction.vop1.has_value() &&
           !instruction.vop2.has_value() &&
           instruction.word_count == 2U;
}

struct ExportTarget {
    ShaderIrExportTargetKind kind =
        ShaderIrExportTargetKind::mrt;
    std::uint8_t index = 0;
};

[[nodiscard]] std::optional<ExportTarget>
export_target(std::uint8_t target) noexcept {
    if (target <= 0x07U) {
        return ExportTarget{
            .kind = ShaderIrExportTargetKind::mrt,
            .index = target,
        };
    }
    if (target == 0x08U) {
        return ExportTarget{
            .kind = ShaderIrExportTargetKind::mrt_z,
            .index = 0U,
        };
    }
    if (target == 0x09U) {
        return ExportTarget{
            .kind = ShaderIrExportTargetKind::null_target,
            .index = 0U,
        };
    }
    if (target >= 0x0cU && target <= 0x0fU) {
        return ExportTarget{
            .kind = ShaderIrExportTargetKind::position,
            .index =
                static_cast<std::uint8_t>(
                    target - 0x0cU),
        };
    }
    if (target == 0x14U) {
        return ExportTarget{
            .kind = ShaderIrExportTargetKind::primitive,
            .index = 0U,
        };
    }
    if (target >= 0x20U && target <= 0x3fU) {
        return ExportTarget{
            .kind = ShaderIrExportTargetKind::parameter,
            .index =
                static_cast<std::uint8_t>(
                    target - 0x20U),
        };
    }
    return std::nullopt;
}

[[nodiscard]] bool plain_sgpr_selector(
    std::uint8_t selector) noexcept {
    return selector <= 105U;
}

[[nodiscard]] bool plain_sgpr_pair_selector(
    std::uint8_t selector) noexcept {
    return selector <= 104U &&
           (selector % 2U) == 0U;
}

[[nodiscard]] std::optional<ShaderIrSpecialScalarSource32>
special_scalar_source32(
    std::uint8_t selector) noexcept {
    using Kind = ShaderIrSpecialScalarSourceKind32;

    switch (selector) {
    case 106U:
        return ShaderIrSpecialScalarSource32{
            .kind = Kind::vcc_lo,
        };
    case 107U:
        return ShaderIrSpecialScalarSource32{
            .kind = Kind::vcc_hi,
        };
    case 124U:
        return ShaderIrSpecialScalarSource32{
            .kind = Kind::m0,
        };
    case 125U:
        return ShaderIrSpecialScalarSource32{
            .kind = Kind::null_register,
        };
    case 126U:
        return ShaderIrSpecialScalarSource32{
            .kind = Kind::exec_lo,
        };
    case 127U:
        return ShaderIrSpecialScalarSource32{
            .kind = Kind::exec_hi,
        };
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<ShaderIrScalarSource32>
scalar_source32(
    std::uint8_t selector,
    std::optional<std::uint32_t> literal_constant) noexcept {
    if (plain_sgpr_selector(selector)) {
        return ShaderIrScalarSource32{
            ShaderIrSgpr{
                .index = selector,
            }};
    }

    if (auto special = special_scalar_source32(selector);
        special.has_value()) {
        return ShaderIrScalarSource32{
            special.value()};
    }

    if (selector >= 128U && selector <= 192U) {
        return ShaderIrScalarSource32{
            ShaderIrInlineInteger32{
                .value =
                    static_cast<std::int32_t>(selector) -
                    128,
            }};
    }

    if (selector >= 193U && selector <= 208U) {
        return ShaderIrScalarSource32{
            ShaderIrInlineInteger32{
                .value =
                    -(
                        static_cast<std::int32_t>(selector) -
                        192),
            }};
    }

    if (selector == 255U &&
        literal_constant.has_value()) {
        return ShaderIrScalarSource32{
            ShaderIrLiteral32{
                .bits = literal_constant.value(),
            }};
    }

    return std::nullopt;
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
            auto semantic_source =
                scalar_source32(
                    source,
                    instruction.sop1->literal_constant);
            if (plain_sgpr_selector(destination) &&
                semantic_source.has_value()) {
                operation =
                    ShaderIrScalarMove32{
                        .destination =
                            ShaderIrSgpr{
                                .index = destination,
                            },
                        .source =
                            std::move(
                                semantic_source.value()),
                    };
            } else {
                operation =
                    unsupported(
                        ShaderIrUnsupportedReason::
                            unsupported_scalar_operand);
            }
        }
        break;

    case Rdna2InstructionKind::s_mov_b64:
        if (valid_sop1_source(instruction)) {
            const auto destination =
                instruction.sop1->destination_selector;
            const auto source =
                instruction.sop1->source_selector;
            if (plain_sgpr_pair_selector(destination) &&
                plain_sgpr_pair_selector(source)) {
                operation =
                    ShaderIrScalarMove64{
                        .destination =
                            ShaderIrSgprPair{
                                .first_index = destination,
                            },
                        .source =
                            ShaderIrSgprPair{
                                .first_index = source,
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

    case Rdna2InstructionKind::v_mov_b32:
        if (valid_vop1_source(instruction)) {
            const auto source =
                instruction.vop1->source_selector;
            if (source >= 256U && source <= 511U) {
                operation =
                    ShaderIrVectorMove32{
                        .destination =
                            ShaderIrVgpr{
                                .index =
                                    instruction.vop1->
                                        destination_selector,
                            },
                        .source =
                            ShaderIrVgpr{
                                .index =
                                    static_cast<std::uint8_t>(
                                        source - 256U),
                            },
                    };
            } else {
                operation =
                    unsupported(
                        ShaderIrUnsupportedReason::
                            unsupported_vector_operand);
            }
        }
        break;

    case Rdna2InstructionKind::v_add_f32:
        if (valid_vop2_source(instruction)) {
            const auto source0 =
                instruction.vop2->source0_selector;
            if (source0 >= 256U && source0 <= 511U) {
                operation =
                    ShaderIrVectorAddF32{
                        .destination =
                            ShaderIrVgpr{
                                .index =
                                    instruction.vop2->
                                        destination_selector,
                            },
                        .source0 =
                            ShaderIrVgpr{
                                .index =
                                    static_cast<std::uint8_t>(
                                        source0 - 256U),
                            },
                        .source1 =
                            ShaderIrVgpr{
                                .index =
                                    instruction.vop2->
                                        source1_selector,
                            },
                    };
            } else {
                operation =
                    unsupported(
                        ShaderIrUnsupportedReason::
                            unsupported_vector_operand);
            }
        }
        break;

    case Rdna2InstructionKind::exp:
        if (valid_exp_source(instruction)) {
            const auto target =
                export_target(
                    instruction.exp->target);
            if (!target.has_value()) {
                operation =
                    unsupported(
                        ShaderIrUnsupportedReason::
                            unknown_export_target);
                break;
            }

            operation =
                ShaderIrExport{
                    .target_kind = target->kind,
                    .target_index = target->index,
                    .enable_mask =
                        instruction.exp->enable_mask,
                    .compressed =
                        instruction.exp->compressed,
                    .done =
                        instruction.exp->done,
                    .valid_mask =
                        instruction.exp->valid_mask,
                    .sources = {
                        ShaderIrVgpr{
                            .index =
                                instruction.exp->
                                    source_vgprs[0]},
                        ShaderIrVgpr{
                            .index =
                                instruction.exp->
                                    source_vgprs[1]},
                        ShaderIrVgpr{
                            .index =
                                instruction.exp->
                                    source_vgprs[2]},
                        ShaderIrVgpr{
                            .index =
                                instruction.exp->
                                    source_vgprs[3]},
                    },
                };
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

    case Rdna2InstructionKind::unknown_vop1_opcode:
        if (valid_vop1_source(instruction)) {
            operation =
                unsupported(
                    ShaderIrUnsupportedReason::
                        unknown_vop1_opcode);
        }
        break;

    case Rdna2InstructionKind::unknown_vop2_opcode:
        if (valid_vop2_source(instruction)) {
            operation =
                unsupported(
                    ShaderIrUnsupportedReason::
                        unknown_vop2_opcode);
        }
        break;

    case Rdna2InstructionKind::unsupported_encoding:
        if (instruction.format ==
                Rdna2InstructionFormat::unsupported &&
            !instruction.sopp.has_value() &&
            !instruction.sop1.has_value() &&
            !instruction.vop1.has_value() &&
            !instruction.vop2.has_value() &&
            !instruction.exp.has_value()) {
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
