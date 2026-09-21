#pragma once

#include <compare>
#include <cstdint>

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

[[nodiscard]] SceJumpSlotApplyResultType
apply_synthetic_jump_slot_patch(
    const SceJumpSlotPatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept;

}  // namespace astraea::execution
