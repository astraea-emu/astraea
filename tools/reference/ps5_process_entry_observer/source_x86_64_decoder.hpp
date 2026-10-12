#pragma once

// Research-only source-backed x86-64 instruction decoding.
// No native guest entry, register mutation, memory access, or CPU execution.
// Decoder != emulator. The caller owns source identity and instruction budget.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <astraea/core/result.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::research {

inline constexpr std::size_t kC1MaxDecodedInstructions = 12U;
inline constexpr std::size_t kX86MaximumInstructionBytes = 15U;

enum class SourceDecodeError {
    invalid_budget,
    decoder_initialization_failure,
    target_not_executable,
    source_unavailable,
    invalid_encoding,
    invalid_decoded_length,
    guest_rip_overflow,
};

struct DecodedSourceInstruction {
    std::uint64_t rip = 0U;
    std::uint8_t length = 0U;
    std::array<std::byte, kX86MaximumInstructionBytes> bytes{};
    // Stable Zydis mnemonic ID for typed inspection. Not an execute action.
    std::uint32_t mnemonic = 0U;
    // Conservative *static* dependency metadata for a fallthrough audit.
    // Include implicit registers and memory-address operands. A 32/64-bit
    // unconditional RAX-family write kills a prior RAX return value.
    // These fields do not execute instructions or validate Sony services.
    bool reads_rax = false;
    bool overwrites_full_rax = false;
    bool control_flow_boundary = false;
};

struct SourceDecodeReport {
    std::array<DecodedSourceInstruction, kC1MaxDecodedInstructions> instructions{};
    std::size_t count = 0U;
    std::uint64_t next_rip = 0U;
};

using SourceDecodeResult =
    astraea::core::Result<SourceDecodeReport, SourceDecodeError>;

// Strict in-order decode of exactly [1,12] instructions at an executable
// GuestImage address. All bytes come from the source initialized image view;
// source-file bounds and guest executable ownership are checked independently
// of Zydis. No speculative operand reads or instruction execution.
[[nodiscard]] SourceDecodeResult decode_source_x86_64(
    const astraea::loader::GuestImage& image,
    std::uint64_t first_rip,
    std::size_t instruction_budget);

[[nodiscard]] std::string_view source_mnemonic_name(
    std::uint32_t id) noexcept;

} // namespace astraea::research
