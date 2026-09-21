#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::uint32_t kX86_64JumpSlotRelocationType = 7;
inline constexpr std::size_t kX86_64JumpSlotWidth = 8;

enum class SceJumpSlotPatchErrorCode {
    unsupported_table_kind,
    unsupported_relocation_type,
    unbound_gate_slot,
    gate_function_mismatch,
    gate_address_unavailable,
};

struct SceJumpSlotPatchError {
    SceJumpSlotPatchErrorCode code =
        SceJumpSlotPatchErrorCode::unsupported_table_kind;
    std::uint32_t gate_slot = 0;
    std::uint32_t relocation_type = 0;
    std::optional<HleFunctionId> expected_function_id;
    std::optional<HleFunctionId> actual_function_id;

    auto operator<=>(const SceJumpSlotPatchError&) const = default;
};

struct SceJumpSlotPatch {
    astraea::memory::GuestAddress relocation_target;
    astraea::memory::GuestAddress gate_destination;
    std::uint32_t gate_slot = 0;
    HleFunctionId function_id;
    std::array<std::byte, kX86_64JumpSlotWidth> bytes{};
    std::optional<std::int64_t> raw_addend;

    auto operator<=>(const SceJumpSlotPatch&) const = default;
};

using SceJumpSlotPatchResult =
    astraea::core::Result<
        SceJumpSlotPatch,
        SceJumpSlotPatchError>;

// Builds, but does not apply, the portable bytes for a synthetic x86-64
// R_X86_64_JUMP_SLOT relocation.
//
// The caller selects the exact synthetic gate slot. No automatic gate
// allocation or resolver precedence is introduced here.
[[nodiscard]] SceJumpSlotPatchResult
build_synthetic_x86_64_jump_slot_patch(
    const SceImportResolutionPlan& plan,
    const SyntheticGateRegion& gate_region,
    std::uint32_t gate_slot) noexcept;

}  // namespace astraea::execution
