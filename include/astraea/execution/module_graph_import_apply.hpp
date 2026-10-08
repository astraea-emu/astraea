#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/module_graph_import_patch.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

struct OwnedModuleImportApplyResult {
    astraea::memory::GuestAddress target;
    astraea::memory::GuestAddress provider_guest_address;

    auto operator<=>(const OwnedModuleImportApplyResult&) const = default;
};

using OwnedModuleImportApplyResultType =
    astraea::core::Result<
        OwnedModuleImportApplyResult, GuestMemoryError>;

// Apply ONE already validated, independently owned x86-64 absolute import
// patch to prepared guest memory. GuestMemoryAccess enforces both guest
// mappings and prepared host permissions. No retail PS5 loader admission,
// runtime module initialization or provider-lifetime semantics are implied.
[[nodiscard]] OwnedModuleImportApplyResultType
apply_owned_module_import_patch(
    const OwnedModuleAbsolutePatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept;


enum class OwnedModuleImportBatchErrorCode {
    too_many_patches,
    conflicting_target,
    preflight_failure,
    apply_failure,
    host_allocation_failure,
};

struct OwnedModuleImportBatchError {
    OwnedModuleImportBatchErrorCode code =
        OwnedModuleImportBatchErrorCode::host_allocation_failure;
    std::size_t patch_index = 0;
    std::optional<std::size_t> conflicting_patch_index;
    std::size_t applied_count = 0;
    std::optional<GuestMemoryError> memory_error;
};

using OwnedModuleImportBatchResult =
    astraea::core::Result<
        std::vector<OwnedModuleImportApplyResult>,
        OwnedModuleImportBatchError>;

// Bounded owned-module batch. Validates every target and rejects overlaps
// BEFORE the first mutation. A post-preflight write failure can still leave
// earlier patches applied: rollback/transactional atomicity is NOT promised.
[[nodiscard]] OwnedModuleImportBatchResult
apply_owned_module_import_batch(
    std::span<const OwnedModuleAbsolutePatch> patches,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
