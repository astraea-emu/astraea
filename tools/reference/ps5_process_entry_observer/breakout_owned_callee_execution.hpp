#pragma once

// C1 experimental non-native x86-64 semantics on an owned synthetic stack.
// No guest instructions enter the host CPU. The first unresolved CALL stops
// before dereferencing its GOT slot, modifying state or invoking a provider.
#include "breakout_offline_entry_interpreter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::research {

enum class OwnedCalleeError {
    decode_refused,
    unexpected_context,
    unsupported_instruction,
    unsupported_operand,
    stack_out_of_bounds,
    address_overflow,
    guest_source_unavailable,
    missing_import_metadata,
    unresolved_import_identity_invalid,
};

struct OwnedCalleeEvidence {
    astraea::execution::GuestCpuContext context{};
    std::uint64_t initial_callee_rsp = 0U;
    std::uint64_t guest_stack_base = 0U;
    std::array<std::byte, 4096U> guest_stack_bytes{};
    std::size_t retired_instructions = 0U;
    std::size_t guest_stack_writes = 0U;
    std::uint64_t stopped_call_rip = 0U;
    std::uint64_t unresolved_got_slot = 0U;
    // Observed raw little-endian GOT word; NEVER an authenticated target.
    std::uint64_t unresolved_got_raw_word = 0U;
    std::uint32_t unresolved_symbol_index = 0U;
    std::string unresolved_bare_nid;
    // This is a typed stop BEFORE CALL; no register, stack or RIP
    // changes associated with the unresolved CALL have been committed.
};

using OwnedCalleeResult = astraea::core::Result<
    OwnedCalleeEvidence, OwnedCalleeError>;

// Executes supported decoded, *general* PUSH/MOV/SUB register/stack operand
// forms through a checked, 4-KiB owned guest stack buffer. Requires an actual
// source-backed decoded CALL [RIP+disp] associated with precisely one ELF
// GLOB_DAT bare-NID relocation; stops BEFORE following the unbound GOT.
// Only entry/callee bytes from the caller-pinned GuestImage are admitted.
// This API does not model Sony process startup, TLS, imported services,
// full x86 semantics, native memory mappings or real game execution.
[[nodiscard]] OwnedCalleeResult run_owned_breakout_callee(
    const astraea::loader::GuestImage& image,
    const BreakoutEntryStepEvidence& entry);

} // namespace astraea::research
