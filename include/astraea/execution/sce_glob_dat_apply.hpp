#pragma once

#include <compare>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/sce_glob_dat_patch.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

struct SceGlobDatApplyResult {
    astraea::memory::GuestAddress
        relocation_target;
    astraea::memory::GuestAddress
        gate_destination;
    std::uint32_t gate_slot = 0;
    HleFunctionId function_id;

    auto operator<=>(
        const SceGlobDatApplyResult&) const =
        default;
};

using SceGlobDatApplyResultType =
    astraea::core::Result<
        SceGlobDatApplyResult,
        GuestMemoryError>;

[[nodiscard]] SceGlobDatApplyResultType
apply_synthetic_glob_dat_patch(
    const SceGlobDatPatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept;

}  // namespace astraea::execution
