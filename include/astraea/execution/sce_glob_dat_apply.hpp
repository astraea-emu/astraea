#pragma once

#include <compare>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

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

enum class SceGlobDatApplyBatchErrorCode {
    apply_failure,
    host_allocation_failure,
};

struct SceGlobDatApplyBatchError {
    SceGlobDatApplyBatchErrorCode code =
        SceGlobDatApplyBatchErrorCode::
            host_allocation_failure;
    std::size_t patch_index = 0;
    std::size_t patch_count = 0;
    std::size_t applied_count = 0;
    std::optional<GuestMemoryError>
        memory_error;

    auto operator<=>(const SceGlobDatApplyBatchError&) const =
        default;
};

using SceGlobDatApplyBatchResult =
    astraea::core::Result<
        std::vector<SceGlobDatApplyResult>,
        SceGlobDatApplyBatchError>;

[[nodiscard]] SceGlobDatApplyResultType
apply_synthetic_glob_dat_patch(
    const SceGlobDatPatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept;

// Applies validated patches in order. This operation is intentionally
// non-atomic: on failure, patches before patch_index remain applied.
// Result storage is reserved before the first guest-memory write.
[[nodiscard]] SceGlobDatApplyBatchResult
apply_synthetic_glob_dat_patches(
    std::span<const SceGlobDatPatch> patches,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
