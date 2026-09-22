#include <astraea/execution/sce_jump_slot_patch.hpp>

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

namespace astraea::execution {
namespace {

[[nodiscard]] SceJumpSlotPatchError patch_error(
    SceJumpSlotPatchErrorCode code,
    std::uint32_t gate_slot,
    std::uint32_t relocation_type,
    std::optional<HleFunctionId> expected_function_id =
        std::nullopt,
    std::optional<HleFunctionId> actual_function_id =
        std::nullopt) noexcept {
    return SceJumpSlotPatchError{
        .code = code,
        .gate_slot = gate_slot,
        .relocation_type = relocation_type,
        .expected_function_id = expected_function_id,
        .actual_function_id = actual_function_id,
    };
}

[[nodiscard]] std::array<
    std::byte,
    kX86_64JumpSlotWidth>
encode_word64_little_endian(
    std::uint64_t value) noexcept {
    std::array<
        std::byte,
        kX86_64JumpSlotWidth>
        bytes{};

    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<std::byte>(
                (value >> (index * 8U)) & 0xffU);
    }

    return bytes;
}

}  // namespace

SceJumpSlotPatchResult
build_synthetic_x86_64_jump_slot_patch(
    const SceImportResolutionPlan& plan,
    const SyntheticGateRegion& gate_region,
    std::uint32_t gate_slot) noexcept {
    if (plan.table_kind !=
        astraea::loader::RelocationTableKind::
            plt_rela) {
        return SceJumpSlotPatchResult::failure(
            patch_error(
                SceJumpSlotPatchErrorCode::
                    unsupported_table_kind,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id));
    }

    if (plan.raw_relocation_type !=
        kX86_64JumpSlotRelocationType) {
        return SceJumpSlotPatchResult::failure(
            patch_error(
                SceJumpSlotPatchErrorCode::
                    unsupported_relocation_type,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id));
    }

    const auto* binding =
        gate_region.binding_for_slot(gate_slot);
    if (binding == nullptr) {
        return SceJumpSlotPatchResult::failure(
            patch_error(
                SceJumpSlotPatchErrorCode::
                    unbound_gate_slot,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id));
    }

    if (binding->function_id != plan.function_id) {
        return SceJumpSlotPatchResult::failure(
            patch_error(
                SceJumpSlotPatchErrorCode::
                    gate_function_mismatch,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id,
                binding->function_id));
    }

    const auto gate_address =
        gate_region.slot_address(gate_slot);
    if (!gate_address.has_value()) {
        return SceJumpSlotPatchResult::failure(
            patch_error(
                SceJumpSlotPatchErrorCode::
                    gate_address_unavailable,
                gate_slot,
                plan.raw_relocation_type,
                plan.function_id,
                binding->function_id));
    }

    return SceJumpSlotPatchResult::success(
        SceJumpSlotPatch{
            .relocation_target =
                plan.relocation_target,
            .gate_destination =
                gate_address.value(),
            .gate_slot = gate_slot,
            .function_id = plan.function_id,
            .bytes =
                encode_word64_little_endian(
                    gate_address->value()),
            .raw_addend = plan.raw_addend,
        });
}

SceJumpSlotPatchesResult
build_synthetic_x86_64_jump_slot_patches(
    std::span<const SceImportResolutionPlan> plans,
    const SyntheticGateRegion& gate_region,
    std::span<const std::uint32_t> gate_slots) {
    if (plans.size() != gate_slots.size()) {
        return SceJumpSlotPatchesResult::failure(
            SceJumpSlotBatchError{
                .code =
                    SceJumpSlotBatchErrorCode::
                        gate_slot_count_mismatch,
                .plan_index = 0,
                .plan_count = plans.size(),
                .gate_slot_count = gate_slots.size(),
                .patch_error = std::nullopt,
            });
    }

    std::size_t index = 0;
    try {
        std::vector<SceJumpSlotPatch> patches;
        patches.reserve(plans.size());

        for (; index < plans.size(); ++index) {
            auto patch =
                build_synthetic_x86_64_jump_slot_patch(
                    plans[index],
                    gate_region,
                    gate_slots[index]);
            if (!patch.has_value()) {
                return SceJumpSlotPatchesResult::failure(
                    SceJumpSlotBatchError{
                        .code =
                            SceJumpSlotBatchErrorCode::
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

        return SceJumpSlotPatchesResult::success(
            std::move(patches));
    } catch (const std::bad_alloc&) {
        return SceJumpSlotPatchesResult::failure(
            SceJumpSlotBatchError{
                .code =
                    SceJumpSlotBatchErrorCode::
                        host_allocation_failure,
                .plan_index = index,
                .plan_count = plans.size(),
                .gate_slot_count = gate_slots.size(),
                .patch_error = std::nullopt,
            });
    } catch (const std::length_error&) {
        return SceJumpSlotPatchesResult::failure(
            SceJumpSlotBatchError{
                .code =
                    SceJumpSlotBatchErrorCode::
                        host_allocation_failure,
                .plan_index = index,
                .plan_count = plans.size(),
                .gate_slot_count = gate_slots.size(),
                .patch_error = std::nullopt,
            });
    }
}

}  // namespace astraea::execution
