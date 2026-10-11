#pragma once

// Research-only, deliberately narrow x86-64 entry interpreter.
// Operates on immutable GuestImage bytes and an owned simulated guest stack;
// never transfers control to any native guest or host instruction.
#include <array>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::research {

enum class BreakoutEntryStopError {
    unavailable_source_bytes,
    unexpected_entry_encoding,
    incorrect_synthetic_context,
    entry_address_overflow,
    invalid_guest_stack,
    target_address_overflow,
    target_not_executable,
    target_source_unavailable,
};

struct BreakoutEntryStepEvidence {
    astraea::execution::GuestCpuContext context{};
    std::uint64_t entry_rip = 0U;
    std::uint64_t initial_rsp = 0U;
    std::uint64_t return_rip = 0U;
    std::uint64_t branch_target = 0U;
    std::uint64_t guest_stack_write_address = 0U;
    std::array<std::byte, 8U> guest_stack_return_word{};
    std::array<std::byte, 9U> source_instruction_bytes{};
    std::byte first_uninterpreted_byte{};
    // AF is architecturally undefined after AND. The stated mask excludes AF.
    std::uint64_t known_rflags_mask = 0U;
    std::uint64_t known_rflags_value = 0U;
    std::size_t interpreted_instructions = 0U;
};

using BreakoutEntryStepResult = astraea::core::Result<
    BreakoutEntryStepEvidence, BreakoutEntryStopError>;

// Recognizes precisely the source-backed x86-64 instructions
//   48 83 e4 f0        AND RSP, sign_extended_imm8(-16)
//   e8 xx xx xx xx     CALL rel32
// in the *exact caller-pinned ELF*. Unsupported encodings refuse closed.
// No generic x86 coverage, indirect calls, HLE, guest/native memory mappings,
// real PS5 process-entry ABI, or execution beyond this two-instruction bound.
[[nodiscard]] BreakoutEntryStepResult interpret_pinned_breakout_entry(
    const astraea::loader::GuestImage& image);

} // namespace astraea::research
