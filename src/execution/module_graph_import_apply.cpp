#include <astraea/execution/module_graph_import_apply.hpp>

#include <span>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

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


OwnedModuleImportBatchResult apply_owned_module_import_batch(
    std::span<const OwnedModuleAbsolutePatch> patches,
    const GuestMemoryAccess& guest_memory) {
    constexpr std::size_t kMaxOwnedImportPatches = 256U;
    const auto error = [](
        OwnedModuleImportBatchErrorCode code,
        std::size_t index,
        std::optional<std::size_t> conflict,
        std::size_t applied,
        std::optional<GuestMemoryError> memory)
        -> OwnedModuleImportBatchResult {
        return OwnedModuleImportBatchResult::failure(
            OwnedModuleImportBatchError{
                .code = code,
                .patch_index = index,
                .conflicting_patch_index = conflict,
                .applied_count = applied,
                .memory_error = std::move(memory),
            });
    };

    if (patches.size() > kMaxOwnedImportPatches) {
        return error(
            OwnedModuleImportBatchErrorCode::too_many_patches,
            patches.size(), std::nullopt, 0U, std::nullopt);
    }

    // Validate every target before the first guest write. A valid mapped
    // range must contain all eight bytes and be writable in the prepared
    // memory. No mutation occurs during preflight.
    for (std::size_t index = 0; index < patches.size(); ++index) {
        const auto permitted = guest_memory.preflight_write(
            patches[index].target, patches[index].bytes.size());
        if (!permitted.has_value()) {
            return error(
                OwnedModuleImportBatchErrorCode::preflight_failure,
                index, std::nullopt, 0U, permitted.error());
        }
    }

    // Each target has already passed checked no-overflow range validation.
    // Reject overlapping writes even when their supplied byte values agree.
    for (std::size_t index = 0; index < patches.size(); ++index) {
        const auto first = astraea::memory::GuestRange::create(
            patches[index].target,
            astraea::memory::GuestSize{patches[index].bytes.size()});
        if (!first.has_value()) {
            return error(
                OwnedModuleImportBatchErrorCode::preflight_failure,
                index, std::nullopt, 0U,
                GuestMemoryError{
                    .code = GuestMemoryErrorCode::guest_memory_range_overflow,
                    .has_guest_address = true,
                    .guest_address = patches[index].target.value(),
                });
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            const auto second = astraea::memory::GuestRange::create(
                patches[previous].target,
                astraea::memory::GuestSize{patches[previous].bytes.size()});
            if (second.has_value() &&
                first.value().overlaps(second.value())) {
                return error(
                    OwnedModuleImportBatchErrorCode::conflicting_target,
                    index, previous, 0U, std::nullopt);
            }
        }
    }

    try {
        std::vector<OwnedModuleImportApplyResult> applied;
        applied.reserve(patches.size());
        for (std::size_t index = 0; index < patches.size(); ++index) {
            const auto written =
                apply_owned_module_import_patch(patches[index], guest_memory);
            if (!written.has_value()) {
                // A concurrent mapping/permission change or a write
                // failure can still occur after all preflight checks.
                // Do NOT claim a rollback that has not happened.
                return error(
                    OwnedModuleImportBatchErrorCode::apply_failure,
                    index, std::nullopt, applied.size(), written.error());
            }
            applied.push_back(written.value());
        }
        return OwnedModuleImportBatchResult::success(std::move(applied));
    } catch (const std::bad_alloc&) {
        return error(
            OwnedModuleImportBatchErrorCode::host_allocation_failure,
            0U, std::nullopt, 0U, std::nullopt);
    } catch (const std::length_error&) {
        return error(
            OwnedModuleImportBatchErrorCode::host_allocation_failure,
            0U, std::nullopt, 0U, std::nullopt);
    }
}


OwnedLiveJumpSlotBatchResult apply_live_owned_jump_slot_batch(
    std::span<const OwnedModuleAbsolutePatch> patches,
    const GuestMemoryAccess& guest_memory) {
    constexpr std::size_t kMaxOwnedImportPatches = 256U;
    constexpr std::uint32_t kJumpSlot = 7U;

    const auto refuse = [](
        OwnedLiveJumpSlotErrorCode code,
        std::size_t index,
        std::size_t applied = 0U,
        std::optional<OwnedModuleImportBatchError> batch =
            std::nullopt) -> OwnedLiveJumpSlotBatchResult {
        return OwnedLiveJumpSlotBatchResult::failure(
            OwnedLiveJumpSlotError{
                .code = code,
                .patch_index = index,
                .applied_count = applied,
                .batch_error = std::move(batch),
            });
    };

    if (patches.size() > kMaxOwnedImportPatches) {
        return refuse(
            OwnedLiveJumpSlotErrorCode::too_many_patches,
            patches.size());
    }

    // All source-side checks are done before the existing batch writer
    // receives any patch. Source existence is deliberately distinct from
    // a syntactically valid ModuleGraph symbol address.
    for (std::size_t index = 0U; index < patches.size(); ++index) {
        const auto& patch = patches[index];
        if (patch.raw_relocation_type != kJumpSlot) {
            return refuse(
                OwnedLiveJumpSlotErrorCode::unsupported_relocation_type,
                index);
        }

        if (!patch.raw_addend.has_value() ||
            patch.source_symbol_address.value() == 0U) {
            return refuse(
                OwnedLiveJumpSlotErrorCode::invalid_patch_encoding,
                index);
        }
        const auto source = patch.source_symbol_address.value();
        for (std::size_t byte_index = 0U;
             byte_index < patch.bytes.size(); ++byte_index) {
            const auto expected = static_cast<std::byte>(
                (source >> (8U * byte_index)) & 0xffU);
            if (patch.bytes[byte_index] != expected) {
                return refuse(
                    OwnedLiveJumpSlotErrorCode::invalid_patch_encoding,
                    index);
            }
        }

        if (!guest_memory.is_exact_executable_address(
                patch.source_symbol_address)) {
            return refuse(
                OwnedLiveJumpSlotErrorCode::
                    provider_not_live_executable,
                index);
        }
    }

    // Preserve the existing full-target writable preflight and overlap
    // rejection. Do not promise rollback if an actual write fails later.
    auto applied = apply_owned_module_import_batch(
        patches, guest_memory);
    if (!applied.has_value()) {
        const auto previous = applied.error();
        return refuse(
            OwnedLiveJumpSlotErrorCode::batch_failure,
            previous.patch_index,
            previous.applied_count,
            previous);
    }
    return OwnedLiveJumpSlotBatchResult::success(
        std::move(applied.value()));
}

}  // namespace astraea::execution
