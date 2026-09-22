#include <astraea/execution/sce_glob_dat_apply.hpp>

#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace astraea::execution {

SceGlobDatApplyResultType
apply_synthetic_glob_dat_patch(
    const SceGlobDatPatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept {
    const auto written =
        guest_memory.write(
            patch.relocation_target,
            std::span<const std::byte>{
                patch.bytes.data(),
                patch.bytes.size()});
    if (!written.has_value()) {
        return SceGlobDatApplyResultType::
            failure(written.error());
    }

    return SceGlobDatApplyResultType::success(
        SceGlobDatApplyResult{
            .relocation_target =
                patch.relocation_target,
            .gate_destination =
                patch.gate_destination,
            .gate_slot = patch.gate_slot,
            .function_id =
                patch.function_id,
        });
}

SceGlobDatApplyBatchResult
apply_synthetic_glob_dat_patches(
    std::span<const SceGlobDatPatch> patches,
    const GuestMemoryAccess& guest_memory) {
    std::vector<SceGlobDatApplyResult> applied;
    try {
        applied.reserve(patches.size());
    } catch (const std::bad_alloc&) {
        return SceGlobDatApplyBatchResult::failure(
            SceGlobDatApplyBatchError{
                .code =
                    SceGlobDatApplyBatchErrorCode::
                        host_allocation_failure,
                .patch_index = 0,
                .patch_count = patches.size(),
                .applied_count = 0,
                .memory_error = std::nullopt,
            });
    } catch (const std::length_error&) {
        return SceGlobDatApplyBatchResult::failure(
            SceGlobDatApplyBatchError{
                .code =
                    SceGlobDatApplyBatchErrorCode::
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
            apply_synthetic_glob_dat_patch(
                patches[index],
                guest_memory);
        if (!result.has_value()) {
            return SceGlobDatApplyBatchResult::failure(
                SceGlobDatApplyBatchError{
                    .code =
                        SceGlobDatApplyBatchErrorCode::
                            apply_failure,
                    .patch_index = index,
                    .patch_count = patches.size(),
                    .applied_count = index,
                    .memory_error = result.error(),
                });
        }

        applied.push_back(result.value());
    }

    return SceGlobDatApplyBatchResult::success(
        std::move(applied));
}

}  // namespace astraea::execution
