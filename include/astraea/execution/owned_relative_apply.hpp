#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

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



enum class OwnedRelativeBatchErrorCode {
    too_many_patches,
    invalid_patch_encoding,
    conflicting_target,
    preflight_failure,
    apply_failure,
    readback_failure,
    readback_mismatch,
    host_allocation_failure,
};

struct OwnedRelativeBatchError {
    OwnedRelativeBatchErrorCode code =
        OwnedRelativeBatchErrorCode::preflight_failure;
    std::size_t patch_index = 0U;
    std::optional<std::size_t> conflicting_patch_index;
    std::size_t applied_count = 0U;
    std::optional<GuestMemoryError> memory_error;
};

using OwnedRelativeBatchResult =
    astraea::core::Result<
        std::vector<OwnedRelativeApplyResult>, OwnedRelativeBatchError>;

// Bounded RELA/RELATIVE relocation application for source-owned images.
// Reconstructs each canonical B+A encoding; preflights *all* target writes
// and readback access and rejects overlaps before the first mutation.
// Verifies each patched word after writing. A post-preflight failure can
// still leave earlier patches applied: applied_count reports that state;
// rollback/transactional atomicity is NOT promised. No retail entry.
[[nodiscard]] OwnedRelativeBatchResult apply_owned_relative_batch(
    std::span<const OwnedRelativePatch> patches,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
