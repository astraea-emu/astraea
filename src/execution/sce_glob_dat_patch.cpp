#include <astraea/execution/sce_glob_dat_patch.hpp>

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

namespace astraea::execution {
namespace {

[[nodiscard]] SceGlobDatPatchError patch_error(
    SceGlobDatPatchErrorCode code,
    std::uint32_t gate_slot,
    std::uint32_t relocation_type,
    std::optional<HleFunctionId>
        expected_function_id =
            std::nullopt,
    std::optional<HleFunctionId>
        actual_function_id =
            std::nullopt) noexcept {
    return SceGlobDatPatchError{
        .code = code,
        .gate_slot = gate_slot,
        .relocation_type = relocation_type,
        .expected_function_id =
            expected_function_id,
        .actual_function_id =
            actual_function_id,
    };
}

[[nodiscard]] std::array<
    std::byte,
    kX86_64GlobDatWidth>
encode_word64_little_endian(
    std::uint64_t value) noexcept {
    std::array<
        std::byte,
        kX86_64GlobDatWidth>
        bytes{};

    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<std::byte>(
                (value >> (index * 8U)) &
                0xffU);
    }

    return bytes;
}

}  // namespace

SceGlobDatPatchResult
build_synthetic_x86_64_glob_dat_gate_patch(
    const SceImportResolutionPlan& plan,
    const SyntheticGateRegion& gate_region,
    std::uint32_t gate_slot) noexcept {
    if (plan.table_kind !=
        astraea::loader::
            RelocationTableKind::rela) {
        return SceGlobDatPatchResult::failure(
            patch_error(
                SceGlobDatPatchErrorCode::
                    unsupported_table_kind,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id));
    }

    if (plan.raw_relocation_type !=
        kX86_64GlobDatRelocationType) {
        return SceGlobDatPatchResult::failure(
            patch_error(
                SceGlobDatPatchErrorCode::
                    unsupported_relocation_type,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id));
    }

    const auto* binding =
        gate_region.binding_for_slot(
            gate_slot);
    if (binding == nullptr) {
        return SceGlobDatPatchResult::failure(
            patch_error(
                SceGlobDatPatchErrorCode::
                    unbound_gate_slot,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id));
    }

    if (binding->function_id !=
        plan.function_id) {
        return SceGlobDatPatchResult::failure(
            patch_error(
                SceGlobDatPatchErrorCode::
                    gate_function_mismatch,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id,
                binding->function_id));
    }

    const auto gate_address =
        gate_region.slot_address(gate_slot);
    if (!gate_address.has_value()) {
        return SceGlobDatPatchResult::failure(
            patch_error(
                SceGlobDatPatchErrorCode::
                    gate_address_unavailable,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id,
                binding->function_id));
    }

    return SceGlobDatPatchResult::success(
        SceGlobDatPatch{
            .relocation_target =
                plan.relocation_target,
            .gate_destination =
                gate_address.value(),
            .gate_slot = gate_slot,
            .function_id =
                plan.function_id,
            .bytes =
                encode_word64_little_endian(
                    gate_address->value()),
            .raw_addend = plan.raw_addend,
        });
}

SceGlobDatPatchesResult
build_synthetic_x86_64_glob_dat_gate_patches(
    std::span<const SceImportResolutionPlan> plans,
    const SyntheticGateRegion& gate_region,
    std::span<const std::uint32_t> gate_slots) {
    if (plans.size() != gate_slots.size()) {
        return SceGlobDatPatchesResult::failure(
            SceGlobDatBatchError{
                .code =
                    SceGlobDatBatchErrorCode::
                        gate_slot_count_mismatch,
                .plan_index = 0,
                .plan_count = plans.size(),
                .gate_slot_count = gate_slots.size(),
                .patch_error = std::nullopt,
            });
    }

    std::size_t index = 0;
    try {
        std::vector<SceGlobDatPatch> patches;
        patches.reserve(plans.size());

        for (; index < plans.size(); ++index) {
            auto patch =
                build_synthetic_x86_64_glob_dat_gate_patch(
                    plans[index],
                    gate_region,
                    gate_slots[index]);
            if (!patch.has_value()) {
                return SceGlobDatPatchesResult::failure(
                    SceGlobDatBatchError{
                        .code =
                            SceGlobDatBatchErrorCode::
                                patch_failure,
                        .plan_index = index,
                        .plan_count = plans.size(),
                        .gate_slot_count =
                            gate_slots.size(),
                        .patch_error = patch.error(),
                    });
            }

            patches.push_back(
                std::move(patch.value()));
        }

        return SceGlobDatPatchesResult::success(
            std::move(patches));
    } catch (const std::bad_alloc&) {
        return SceGlobDatPatchesResult::failure(
            SceGlobDatBatchError{
                .code =
                    SceGlobDatBatchErrorCode::
                        host_allocation_failure,
                .plan_index = index,
                .plan_count = plans.size(),
                .gate_slot_count = gate_slots.size(),
                .patch_error = std::nullopt,
            });
    } catch (const std::length_error&) {
        return SceGlobDatPatchesResult::failure(
            SceGlobDatBatchError{
                .code =
                    SceGlobDatBatchErrorCode::
                        host_allocation_failure,
                .plan_index = index,
                .plan_count = plans.size(),
                .gate_slot_count = gate_slots.size(),
                .patch_error = std::nullopt,
            });
    }
}

}  // namespace astraea::execution
