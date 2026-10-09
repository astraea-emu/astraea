#pragma once

#include <compare>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/owned_relative_patch.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

struct OwnedRelativeApplyResult {
    astraea::memory::GuestAddress target;
    astraea::memory::GuestAddress relocated_value;

    auto operator<=>(const OwnedRelativeApplyResult&) const = default;
};

using OwnedRelativeApplyResultType =
    astraea::core::Result<OwnedRelativeApplyResult, GuestMemoryError>;

// One prevalidated, independently owned R_X86_64_RELATIVE patch only.
// The caller supplies the verified load bias at patch construction.
// This writes to checked, prepared guest memory without executing code;
// no Sony runtime-loader state or hardware ABI is implied.
[[nodiscard]] OwnedRelativeApplyResultType apply_owned_relative_patch(
    const OwnedRelativePatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept;

}  // namespace astraea::execution
