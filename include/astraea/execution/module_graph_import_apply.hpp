#pragma once

#include <compare>

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

}  // namespace astraea::execution
