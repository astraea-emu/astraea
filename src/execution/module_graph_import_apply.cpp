#include <astraea/execution/module_graph_import_apply.hpp>

#include <span>

namespace astraea::execution {

OwnedModuleImportApplyResultType apply_owned_module_import_patch(
    const OwnedModuleAbsolutePatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept {
    // Never dereference the guest target directly. The checked memory facade
    // requires a valid mapped, writable and prepared eight-byte extent.
    const auto allowed = guest_memory.preflight_write(
        patch.target, patch.bytes.size());
    if (!allowed.has_value()) {
        return OwnedModuleImportApplyResultType::failure(allowed.error());
    }
    const auto written = guest_memory.write(
        patch.target,
        std::span<const std::byte>{
            patch.bytes.data(), patch.bytes.size()});
    if (!written.has_value()) {
        return OwnedModuleImportApplyResultType::failure(written.error());
    }
    return OwnedModuleImportApplyResultType::success(
        OwnedModuleImportApplyResult{
            .target = patch.target,
            .provider_guest_address = patch.source_symbol_address,
        });
}

}  // namespace astraea::execution
