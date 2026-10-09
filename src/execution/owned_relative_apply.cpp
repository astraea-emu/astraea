#include <astraea/execution/owned_relative_apply.hpp>

#include <span>

namespace astraea::execution {

OwnedRelativeApplyResultType apply_owned_relative_patch(
    const OwnedRelativePatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept {
    // Always validate the complete writable mapped region first.
    const auto allowed = guest_memory.preflight_write(
        patch.target, patch.bytes.size());
    if (!allowed.has_value()) {
        return OwnedRelativeApplyResultType::failure(allowed.error());
    }
    const auto written = guest_memory.write(
        patch.target, std::span<const std::byte>{
            patch.bytes.data(), patch.bytes.size()});
    if (!written.has_value()) {
        return OwnedRelativeApplyResultType::failure(written.error());
    }
    return OwnedRelativeApplyResultType::success(OwnedRelativeApplyResult{
        .target = patch.target,
        .relocated_value = patch.relocated_value,
    });
}

}  // namespace astraea::execution
