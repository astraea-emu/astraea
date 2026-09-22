#include <astraea/execution/sce_glob_dat_apply.hpp>

#include <span>

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

}  // namespace astraea::execution
