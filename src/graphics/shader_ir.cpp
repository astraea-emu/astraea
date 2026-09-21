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
           instruction.sopp.has_value();
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
            const auto displacement =
                static_cast<std::int32_t>(
                    instruction.sopp->simm16) *
                    4 +
                4;
            operation =
                ShaderIrRelativeBranch{
                    .byte_delta = displacement,
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
