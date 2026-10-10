#include <astraea/execution/owned_relative_apply.hpp>

#include <span>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

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



OwnedRelativeBatchResult apply_owned_relative_batch(
    std::span<const OwnedRelativePatch> patches,
    const GuestMemoryAccess& guest_memory) {
    constexpr std::size_t kMaximumPatches = 4096U;
    const auto refuse = [](
        OwnedRelativeBatchErrorCode code,
        std::size_t index,
        std::size_t applied,
        std::optional<std::size_t> conflict = std::nullopt,
        std::optional<GuestMemoryError> memory_error = std::nullopt)
        -> OwnedRelativeBatchResult {
        return OwnedRelativeBatchResult::failure(
            OwnedRelativeBatchError{
                .code = code,
                .patch_index = index,
                .conflicting_patch_index = conflict,
                .applied_count = applied,
                .memory_error = std::move(memory_error),
            });
    };

    if (patches.size() > kMaximumPatches) {
        return refuse(OwnedRelativeBatchErrorCode::too_many_patches,
                      patches.size(), 0U);
    }

    struct Target {
        std::uint64_t base;
        std::size_t index;
    };

    try {
        std::vector<Target> ordered;
        ordered.reserve(patches.size());
        std::vector<OwnedRelativeApplyResult> applied;
        applied.reserve(patches.size());

        // All source, mapping, permission and readback checks happen
        // before the first guest write.
        for (std::size_t index = 0U; index < patches.size(); ++index) {
            const auto& patch = patches[index];
            const astraea::loader::DynamicRelocation record{
                .table_kind = astraea::loader::RelocationTableKind::rela,
                .table_index = index,
                .target = patch.target,
                .raw_info = kOwnedX86_64RelativeType,
                .symbol_index = 0U,
                .relocation_type = kOwnedX86_64RelativeType,
                .addend = patch.raw_addend,
            };
            const auto encoded = build_owned_x86_64_relative_patch(
                record, patch.load_bias);
            if (!encoded.has_value() || encoded.value() != patch) {
                return refuse(
                    OwnedRelativeBatchErrorCode::invalid_patch_encoding,
                    index, 0U);
            }

            const auto writable =
                guest_memory.preflight_write(patch.target, patch.bytes.size());
            if (!writable.has_value()) {
                return refuse(
                    OwnedRelativeBatchErrorCode::preflight_failure,
                    index, 0U, std::nullopt, writable.error());
            }
            std::array<std::byte, kOwnedX86_64RelativePatchWidth> before{};
            const auto readable = guest_memory.read(patch.target, before);
            if (!readable.has_value()) {
                return refuse(
                    OwnedRelativeBatchErrorCode::preflight_failure,
                    index, 0U, std::nullopt, readable.error());
            }
            ordered.push_back(Target{patch.target.value(), index});
        }

        std::sort(
            ordered.begin(), ordered.end(),
            [](const Target& lhs, const Target& rhs) {
                return lhs.base < rhs.base;
            });
        for (std::size_t i = 1U; i < ordered.size(); ++i) {
            // Canonical patch construction already checked the target
            // word's full eight-byte range for address overflow.
            if (ordered[i].base <
                ordered[i - 1U].base + kOwnedX86_64RelativePatchWidth) {
                return refuse(
                    OwnedRelativeBatchErrorCode::conflicting_target,
                    ordered[i].index, 0U, ordered[i - 1U].index);
            }
        }

        for (std::size_t index = 0U; index < patches.size(); ++index) {
            const auto written =
                apply_owned_relative_patch(patches[index], guest_memory);
            if (!written.has_value()) {
                return refuse(
                    OwnedRelativeBatchErrorCode::apply_failure,
                    index, applied.size(), std::nullopt, written.error());
            }
            applied.push_back(written.value());

            std::array<std::byte, kOwnedX86_64RelativePatchWidth> observed{};
            const auto verified =
                guest_memory.read(patches[index].target, observed);
            if (!verified.has_value()) {
                return refuse(
                    OwnedRelativeBatchErrorCode::readback_failure,
                    index, applied.size(), std::nullopt, verified.error());
            }
            if (observed != patches[index].bytes) {
                return refuse(
                    OwnedRelativeBatchErrorCode::readback_mismatch,
                    index, applied.size());
            }
        }

        return OwnedRelativeBatchResult::success(std::move(applied));
    } catch (const std::bad_alloc&) {
        return refuse(
            OwnedRelativeBatchErrorCode::host_allocation_failure, 0U, 0U);
    } catch (const std::length_error&) {
        return refuse(
            OwnedRelativeBatchErrorCode::host_allocation_failure, 0U, 0U);
    }
}

}  // namespace astraea::execution
