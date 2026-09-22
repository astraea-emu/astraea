#include <astraea/graphics/rdna2_decoder.hpp>

#include <limits>

namespace astraea::graphics {
namespace {

// AMD "RDNA 2" Instruction Set Architecture: Reference Guide,
// document 70648, 30-November-2020, section 13.1.5 / tables 71-72.
//
// SOPP:
//   ENCODING [31:23] = 10_1111111
//   OP       [22:16]
//   SIMM16   [15:0]
//
// This slice classifies the generic RDNA2 SOPP control-flow opcodes:
//   0 = S_NOP
//   1 = S_ENDPGM
//   2 = S_BRANCH
//   4 = S_CBRANCH_SCC0
//   5 = S_CBRANCH_SCC1
//   6 = S_CBRANCH_VCCZ
//   7 = S_CBRANCH_VCCNZ
//   8 = S_CBRANCH_EXECZ
//   9 = S_CBRANCH_EXECNZ
//  10 = S_BARRIER
//
// Opcode 3 (S_WAKEUP) and later control/synchronization instructions remain
// intentionally unclassified until their state semantics are separately scoped.
//
// This is generic RDNA2 ISA evidence. It is not a PS5 shader launch ABI or
// evidence that a particular Sony shader container uses any specific form.

constexpr std::uint32_t kSoppEncoding = 0x17fU;
constexpr std::uint32_t kSoppEncodingMask = 0x1ffU;
constexpr unsigned int kSoppEncodingShift = 23U;
constexpr std::uint32_t kSoppOpcodeMask = 0x7fU;
constexpr unsigned int kSoppOpcodeShift = 16U;
constexpr std::uint32_t kSimm16Mask = 0xffffU;

constexpr std::uint8_t kSoppNopOpcode = 0;
constexpr std::uint8_t kSoppEndpgmOpcode = 1;
constexpr std::uint8_t kSoppBranchOpcode = 2;
constexpr std::uint8_t kSoppCbranchScc0Opcode = 4;
constexpr std::uint8_t kSoppCbranchScc1Opcode = 5;
constexpr std::uint8_t kSoppCbranchVcczOpcode = 6;
constexpr std::uint8_t kSoppCbranchVccnzOpcode = 7;
constexpr std::uint8_t kSoppCbranchExeczOpcode = 8;
constexpr std::uint8_t kSoppCbranchExecnzOpcode = 9;
constexpr std::uint8_t kSoppBarrierOpcode = 10;

[[nodiscard]] Rdna2DecodeError decode_error(
    Rdna2DecodeErrorCode code,
    std::size_t word_index,
    std::size_t available_words) noexcept {
    return Rdna2DecodeError{
        .code = code,
        .word_index = word_index,
        .available_words = available_words,
    };
}

[[nodiscard]] std::array<std::byte, 4> raw_encoding(
    std::uint32_t word) noexcept {
    return {
        static_cast<std::byte>(word & 0xffU),
        static_cast<std::byte>((word >> 8U) & 0xffU),
        static_cast<std::byte>((word >> 16U) & 0xffU),
        static_cast<std::byte>((word >> 24U) & 0xffU),
    };
}

[[nodiscard]] std::int16_t decode_simm16(
    std::uint32_t word) noexcept {
    const auto bits = static_cast<std::uint16_t>(
        word & kSimm16Mask);
    const auto signed_value =
        bits < 0x8000U
            ? static_cast<std::int32_t>(bits)
            : static_cast<std::int32_t>(bits) - 0x10000;
    return static_cast<std::int16_t>(signed_value);
}

[[nodiscard]] Rdna2InstructionKind classify_sopp_opcode(
    std::uint8_t opcode) noexcept {
    switch (opcode) {
    case kSoppNopOpcode:
        return Rdna2InstructionKind::s_nop;
    case kSoppEndpgmOpcode:
        return Rdna2InstructionKind::s_endpgm;
    case kSoppBranchOpcode:
        return Rdna2InstructionKind::s_branch;
    case kSoppCbranchScc0Opcode:
        return Rdna2InstructionKind::s_cbranch_scc0;
    case kSoppCbranchScc1Opcode:
        return Rdna2InstructionKind::s_cbranch_scc1;
    case kSoppCbranchVcczOpcode:
        return Rdna2InstructionKind::s_cbranch_vccz;
    case kSoppCbranchVccnzOpcode:
        return Rdna2InstructionKind::s_cbranch_vccnz;
    case kSoppCbranchExeczOpcode:
        return Rdna2InstructionKind::s_cbranch_execz;
    case kSoppCbranchExecnzOpcode:
        return Rdna2InstructionKind::s_cbranch_execnz;
    case kSoppBarrierOpcode:
        return Rdna2InstructionKind::s_barrier;
    default:
        return Rdna2InstructionKind::unknown_sopp_opcode;
    }
}

}  // namespace

Rdna2DecodeResult decode_rdna2_instruction(
    std::span<const std::uint32_t> words,
    std::size_t word_index) {
    if (word_index >
        std::numeric_limits<std::size_t>::max() / 4U) {
        return Rdna2DecodeResult::failure(
            decode_error(
                Rdna2DecodeErrorCode::source_offset_overflow,
                word_index,
                words.size()));
    }

    if (word_index >= words.size()) {
        return Rdna2DecodeResult::failure(
            decode_error(
                Rdna2DecodeErrorCode::instruction_out_of_bounds,
                word_index,
                words.size()));
    }

    const auto word = words[word_index];
    const auto byte_offset = word_index * 4U;
    const auto encoding =
        (word >> kSoppEncodingShift) &
        kSoppEncodingMask;

    if (encoding != kSoppEncoding) {
        return Rdna2DecodeResult::success(
            Rdna2Instruction{
                .word_index = word_index,
                .byte_offset = byte_offset,
                .raw_word = word,
                .raw_encoding = raw_encoding(word),
                .format =
                    Rdna2InstructionFormat::unsupported,
                .kind =
                    Rdna2InstructionKind::
                        unsupported_encoding,
                .sopp = std::nullopt,
            });
    }

    const auto opcode = static_cast<std::uint8_t>(
        (word >> kSoppOpcodeShift) &
        kSoppOpcodeMask);

    return Rdna2DecodeResult::success(
        Rdna2Instruction{
            .word_index = word_index,
            .byte_offset = byte_offset,
            .raw_word = word,
            .raw_encoding = raw_encoding(word),
            .format = Rdna2InstructionFormat::sopp,
            .kind = classify_sopp_opcode(opcode),
            .sopp =
                Rdna2SoppFields{
                    .opcode = opcode,
                    .simm16 = decode_simm16(word),
                },
        });
}

}  // namespace astraea::graphics
