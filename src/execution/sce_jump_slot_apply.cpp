#include <astraea/execution/sce_jump_slot_apply.hpp>

#include <span>

namespace astraea::execution {

SceJumpSlotApplyResultType
apply_synthetic_jump_slot_patch(
    const SceJumpSlotPatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept {
    const auto written =
        guest_memory.write(
            patch.relocation_target,
            std::span<const std::byte>{
                patch.bytes.data(),
                patch.bytes.size()});
    if (!written.has_value()) {
        return SceJumpSlotApplyResultType::failure(
            written.error());
    }

    return SceJumpSlotApplyResultType::success(
        SceJumpSlotApplyResult{
            .relocation_target =
                patch.relocation_target,
            .gate_destination =
                patch.gate_destination,
            .gate_slot = patch.gate_slot,
            .function_id = patch.function_id,
        });
}

}  // namespace astraea::execution
