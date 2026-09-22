#pragma once

#include <compare>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/sce_jump_slot_patch.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

struct SceJumpSlotApplyResult {
    astraea::memory::GuestAddress relocation_target;
    astraea::memory::GuestAddress gate_destination;
    std::uint32_t gate_slot = 0;
    HleFunctionId function_id;

    auto operator<=>(const SceJumpSlotApplyResult&) const = default;
};

using SceJumpSlotApplyResultType =
    astraea::core::Result<
        SceJumpSlotApplyResult,
        GuestMemoryError>;

enum class SceJumpSlotApplyBatchErrorCode {
    apply_failure,
    host_allocation_failure,
};

struct SceJumpSlotApplyBatchError {
    SceJumpSlotApplyBatchErrorCode code =
        SceJumpSlotApplyBatchErrorCode::host_allocation_failure;
    std::size_t patch_index = 0;
    std::size_t patch_count = 0;
    std::size_t applied_count = 0;
    std::optional<GuestMemoryError> memory_error;

    auto operator<=>(const SceJumpSlotApplyBatchError&) const = default;
};

using SceJumpSlotApplyBatchResult =
    astraea::core::Result<
        std::vector<SceJumpSlotApplyResult>,
        SceJumpSlotApplyBatchError>;

[[nodiscard]] SceJumpSlotApplyResultType
apply_synthetic_jump_slot_patch(
    const SceJumpSlotPatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept;

// Applies validated patches in order. This operation is intentionally
// non-atomic: on failure, patches before patch_index remain applied.
// Result storage is reserved before the first guest-memory write.
[[nodiscard]] SceJumpSlotApplyBatchResult
apply_synthetic_jump_slot_patches(
    std::span<const SceJumpSlotPatch> patches,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
