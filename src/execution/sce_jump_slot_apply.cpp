#include <astraea/execution/sce_jump_slot_apply.hpp>

#include <new>
#include <span>
#include <stdexcept>
#include <utility>

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

SceJumpSlotApplyBatchResult
apply_synthetic_jump_slot_patches(
    std::span<const SceJumpSlotPatch> patches,
    const GuestMemoryAccess& guest_memory) {
    std::vector<SceJumpSlotApplyResult> applied;
    try {
        applied.reserve(patches.size());
    } catch (const std::bad_alloc&) {
        return SceJumpSlotApplyBatchResult::failure(
            SceJumpSlotApplyBatchError{
                .code =
                    SceJumpSlotApplyBatchErrorCode::
                        host_allocation_failure,
                .patch_index = 0,
                .patch_count = patches.size(),
                .applied_count = 0,
                .memory_error = std::nullopt,
            });
    } catch (const std::length_error&) {
        return SceJumpSlotApplyBatchResult::failure(
            SceJumpSlotApplyBatchError{
                .code =
                    SceJumpSlotApplyBatchErrorCode::
                        host_allocation_failure,
                .patch_index = 0,
                .patch_count = patches.size(),
                .applied_count = 0,
                .memory_error = std::nullopt,
            });
    }

    for (std::size_t index = 0;
         index < patches.size();
         ++index) {
        const auto result =
            apply_synthetic_jump_slot_patch(
                patches[index],
                guest_memory);
        if (!result.has_value()) {
            return SceJumpSlotApplyBatchResult::failure(
                SceJumpSlotApplyBatchError{
                    .code =
                        SceJumpSlotApplyBatchErrorCode::
                            apply_failure,
                    .patch_index = index,
                    .patch_count = patches.size(),
                    .applied_count = index,
                    .memory_error = result.error(),
                });
        }

        applied.push_back(result.value());
    }

    return SceJumpSlotApplyBatchResult::success(
        std::move(applied));
}

}  // namespace astraea::execution
