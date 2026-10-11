#pragma once

// Opt-in C1 research CPU stepping, not production guest execution.
// A decoded source instruction may retire only after its operand/memory
// checks succeed. Unknown control transfers are typed pre-retirement stops.
#include "owned_guest_address_space.hpp"
#include "source_x86_64_decoder.hpp"

#include <cstddef>
#include <cstdint>
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

// Supported forms are generic PUSH r64, MOV r64,r64, SUB r64,imm,
// MOV qword [base+disp],imm. CALL and other control transfers are
// *not* executed; an unresolved_control_transfer stop leaves CPU and
// guest memory untouched. Every error leaves CPU unchanged; each retired
// instruction has at most one checked guest-memory write.
[[nodiscard]] OwnedCpuStepResult step_owned_x86_64(
    const DecodedSourceInstruction& record,
    astraea::execution::GuestCpuContext& context,
    OwnedGuestAddressSpace& memory);

} // namespace astraea::research
