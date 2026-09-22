#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::uint32_t
    kX86_64GlobDatRelocationType = 6;
inline constexpr std::size_t
    kX86_64GlobDatWidth = 8;

enum class SceGlobDatPatchErrorCode {
    unsupported_table_kind,
    unsupported_relocation_type,
    unbound_gate_slot,
    gate_function_mismatch,
    gate_address_unavailable,
};

struct SceGlobDatPatchError {
    SceGlobDatPatchErrorCode code =
        SceGlobDatPatchErrorCode::
            unsupported_table_kind;
    std::uint32_t gate_slot = 0;
    std::uint32_t relocation_type = 0;
    std::optional<HleFunctionId>
        expected_function_id;
    std::optional<HleFunctionId>
        actual_function_id;

    auto operator<=>(
        const SceGlobDatPatchError&) const =
        default;
};

struct SceGlobDatPatch {
    astraea::memory::GuestAddress
        relocation_target;
    astraea::memory::GuestAddress
        gate_destination;
    std::uint32_t gate_slot = 0;
    HleFunctionId function_id;
    std::array<
        std::byte,
        kX86_64GlobDatWidth>
        bytes{};
    std::optional<std::int64_t> raw_addend;

    auto operator<=>(
        const SceGlobDatPatch&) const =
        default;
};

using SceGlobDatPatchResult =
    astraea::core::Result<
        SceGlobDatPatch,
        SceGlobDatPatchError>;

enum class SceGlobDatBatchErrorCode {
    gate_slot_count_mismatch,
    patch_failure,
    host_allocation_failure,
};

struct SceGlobDatBatchError {
    SceGlobDatBatchErrorCode code =
        SceGlobDatBatchErrorCode::
            host_allocation_failure;
    std::size_t plan_index = 0;
    std::size_t plan_count = 0;
    std::size_t gate_slot_count = 0;
    std::optional<SceGlobDatPatchError>
        patch_error;

    auto operator<=>(const SceGlobDatBatchError&) const =
        default;
};

using SceGlobDatPatchesResult =
    astraea::core::Result<
        std::vector<SceGlobDatPatch>,
        SceGlobDatBatchError>;

// Builds, but does not apply, an imported-function GLOB_DAT patch whose
// resolved symbol address is an explicitly selected synthetic HLE gate.
//
// x86-64 R_X86_64_GLOB_DAT is word64 S. The raw RELA addend remains
// provenance only and does not change the emitted bytes.
[[nodiscard]] SceGlobDatPatchResult
build_synthetic_x86_64_glob_dat_gate_patch(
    const SceImportResolutionPlan& plan,
    const SyntheticGateRegion& gate_region,
    std::uint32_t gate_slot) noexcept;

// Builds an ordered batch of already-supported GLOB_DAT patches. Gate slots
// are caller-selected explicitly and must be one-to-one with the input plans.
// No gate allocation or guest-memory writes occur here.
[[nodiscard]] SceGlobDatPatchesResult
build_synthetic_x86_64_glob_dat_gate_patches(
    std::span<const SceImportResolutionPlan> plans,
    const SyntheticGateRegion& gate_region,
    std::span<const std::uint32_t> gate_slots);

}  // namespace astraea::execution
