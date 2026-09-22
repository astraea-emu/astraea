#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <astraea/core/result.hpp>

namespace astraea::graphics {

enum class Rdna2InstructionFormat {
    sopp,
    sop1,
    unsupported,
};

enum class Rdna2InstructionKind {
    s_nop,
    s_endpgm,
    s_branch,
    s_cbranch_scc0,
    s_cbranch_scc1,
    s_cbranch_vccz,
    s_cbranch_vccnz,
    s_cbranch_execz,
    s_cbranch_execnz,
    s_barrier,
    s_waitcnt,
    s_mov_b32,
    unknown_sopp_opcode,
    unknown_sop1_opcode,
    unsupported_encoding,
};

enum class Rdna2DecodeErrorCode {
    source_offset_overflow,
    instruction_out_of_bounds,
};

struct Rdna2DecodeError {
    Rdna2DecodeErrorCode code =
        Rdna2DecodeErrorCode::instruction_out_of_bounds;
    std::size_t word_index = 0;
    std::size_t available_words = 0;

    auto operator<=>(const Rdna2DecodeError&) const = default;
};

struct Rdna2SoppFields {
    std::uint8_t opcode = 0;
    std::int16_t simm16 = 0;

    auto operator<=>(const Rdna2SoppFields&) const = default;
};

struct Rdna2Sop1Fields {
    std::uint8_t opcode = 0;
    std::uint8_t destination_selector = 0;
    std::uint8_t source_selector = 0;

    auto operator<=>(const Rdna2Sop1Fields&) const = default;
};

struct Rdna2Instruction {
    std::size_t word_index = 0;
    std::size_t byte_offset = 0;
    std::uint32_t raw_word = 0;
    std::array<std::byte, 4> raw_encoding{};
    Rdna2InstructionFormat format =
        Rdna2InstructionFormat::unsupported;
    Rdna2InstructionKind kind =
        Rdna2InstructionKind::unsupported_encoding;
    std::optional<Rdna2SoppFields> sopp;
    std::optional<Rdna2Sop1Fields> sop1;

    auto operator<=>(const Rdna2Instruction&) const = default;
};

using Rdna2DecodeResult =
    astraea::core::Result<Rdna2Instruction, Rdna2DecodeError>;

// Decodes only the generic RDNA2 SOPP encoding documented by AMD.
// The caller owns shader-container parsing and conversion into 32-bit
// instruction words. This function does not encode PS5 launch-ABI,
// container, descriptor, SPIR-V, or Vulkan assumptions.
[[nodiscard]] Rdna2DecodeResult decode_rdna2_instruction(
    std::span<const std::uint32_t> words,
    std::size_t word_index);

}  // namespace astraea::graphics
