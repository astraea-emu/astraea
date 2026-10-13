#pragma once

// Opt-in C1 research CPU stepping, not production guest execution.
// A decoded source instruction may retire only after its operand/memory
// checks succeed. Unknown control transfers are typed pre-retirement stops.
#include "owned_guest_address_space.hpp"
#include "source_x86_64_decoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>

#include <Zydis/Zydis.h>
#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>

namespace astraea::research {

enum class OwnedCpuStepStopCode {
    decode_refused,
    context_mismatch,
    unsupported_instruction,
    unsupported_operand,
    unresolved_control_transfer,
    address_overflow,
    guest_memory_refused,
};

struct OwnedCpuStepStop {
    OwnedCpuStepStopCode code;
    std::uint64_t guest_rip = 0U;
    std::optional<OwnedGuestMemoryFault> memory_fault;
};

struct OwnedCpuStepCommit {
    std::uint64_t next_rip = 0U;
    std::size_t guest_writes = 0U;
    // Explicit guest-visible write effect of the ONE currently supported
    // write-capable instruction. No host address is ever recorded.
    std::optional<std::uint64_t> guest_write_address;
    // Either zero (no write) or exactly eight bytes for the current ISA
    // subset. Wider/partial/multi-write effects require a new contract.
    std::size_t guest_write_size = 0U;
    std::array<std::byte, 8U> guest_write_bytes{};
};

using OwnedCpuStepResult = astraea::core::Result<
    OwnedCpuStepCommit, OwnedCpuStepStop>;

// Compute a checked *guest virtual* memory operand address. Supports base
// r64 or RIP and signed displacement, but no scaled index/segment override.
// Returns nullopt for unrepresentable/unsupported forms.
[[nodiscard]] std::optional<std::uint64_t> owned_cpu_memory_address(
    const astraea::execution::GuestCpuContext& context,
    const ZydisDecodedOperand& operand,
    std::uint64_t next_rip) noexcept;

// Supported research forms are PUSH r64, MOV r64,r64, SUB r64,imm,
// AND r64,imm and MOV qword [base+disp],imm, plus direct CALL rel32
// after executable-target and writable-return-slot preflight. Indirect
// CALL and other unsupported control transfers stop before retirement.
// This is ONLY non-native modeled guest state, not executable host memory.
// Every error leaves CPU unchanged; each retired instruction has at most
// one checked guest-memory write.
[[nodiscard]] OwnedCpuStepResult step_owned_x86_64(
    const DecodedSourceInstruction& record,
    astraea::execution::GuestCpuContext& context,
    OwnedGuestAddressSpace& memory);


// Hard cap for deterministic research runs even when callers supply size_t max.
inline constexpr std::size_t kC1MaxOwnedRunInstructions = 4096U;

enum class OwnedCpuRunStopCode {
    invalid_budget,
    budget_exhausted,
    fetch_refused,
    invalid_encoding,
    step_refused,
};

// Comparable, caller-owned reference record for evaluating future CPU
// backends on identical original source bytes and guest virtual memory.
// Includes complete committed CPU state and any eight-byte guest store.
// Unsupported instructions produce NO trace event and NO state commit.
struct OwnedCpuTraceStep {
    std::uint64_t before_rip = 0U;
    std::uint64_t after_rip = 0U;
    astraea::execution::GuestCpuContext after_context{};
    std::optional<std::uint64_t> guest_write_address;
    // Either zero (no write) or exactly eight bytes for the current ISA
    // subset. Wider/partial/multi-write effects require a new contract.
    std::size_t guest_write_size = 0U;
    std::array<std::byte, 8U> guest_write_bytes{};

    auto operator<=>(const OwnedCpuTraceStep&) const = default;
};

struct OwnedCpuRunReport {
    std::size_t retired_instructions = 0U;
    std::size_t guest_writes = 0U;
    std::size_t trace_steps = 0U;
    OwnedCpuRunStopCode stop = OwnedCpuRunStopCode::invalid_budget;
    std::uint64_t stop_rip = 0U;
    std::optional<OwnedGuestMemoryFault> memory_fault;
    std::optional<OwnedCpuStepStop> step_stop;
};

// Execute at most instruction_budget supported instructions, fetching fresh
// source bytes via execute-permitted guest virtual memory on EVERY step.
// The returned stop is always explicit: budget, decode/fetch refusal or
// step refusal. A stop never retires its own instruction; previous completed
// instructions remain committed (not a multi-step transaction).
// This does not invoke native guest code, resolve imports or infer PS5 ABI.
// An optional, caller-owned trace span must hold the ENTIRE requested
// instruction budget; a too-small nonempty span is refused before execution.
// Only successfully retired instructions are recorded. No allocation occurs
// inside the runner and no trace is emitted for a stopped instruction.
[[nodiscard]] OwnedCpuRunReport run_owned_x86_64(
    astraea::execution::GuestCpuContext& context,
    OwnedGuestAddressSpace& memory,
    std::size_t instruction_budget,
    std::span<OwnedCpuTraceStep> trace = {});

} // namespace astraea::research
