#pragma once

// Research-only observation of an unretired RIP-relative indirect CALL.
// Reads an operand from checked guest virtual memory without transferring
// control, assuming any provider identity, or invoking host/native code.
#include "owned_guest_address_space.hpp"
#include "source_x86_64_decoder.hpp"

#include <array>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>

namespace astraea::research {

enum class OwnedIndirectCallErrorCode {
    context_mismatch,
    invalid_encoding,
    source_fetch_refused,
    source_bytes_mismatch,
    unsupported_call_form,
    address_overflow,
    operand_read_refused,
};

struct OwnedIndirectCallError {
    OwnedIndirectCallErrorCode code;
    std::uint64_t call_rip = 0U;
    std::optional<OwnedGuestMemoryFault> memory_fault;
};

struct OwnedIndirectCallObservation {
    std::uint64_t call_rip = 0U;
    std::uint64_t return_rip = 0U;
    std::uint64_t pointer_guest_address = 0U;
    std::array<std::byte, 8U> pointer_word_bytes{};
    std::uint64_t pointer_word_le = 0U;
};

using OwnedIndirectCallResult = astraea::core::Result<
    OwnedIndirectCallObservation, OwnedIndirectCallError>;

// Only CALL qword ptr [RIP+disp] is observed, not executed. Instruction
// bytes must equal current execute-permitted virtual memory; the eight-byte
// pointer is read under guest *read* permission. Its numeric contents are
// opaque and NOT a verified code target. CPU and guest state are unchanged
// on both success and refusal.
[[nodiscard]] OwnedIndirectCallResult observe_owned_indirect_call(
    const DecodedSourceInstruction& source_record,
    const astraea::execution::GuestCpuContext& context,
    const OwnedGuestAddressSpace& memory);

} // namespace astraea::research
