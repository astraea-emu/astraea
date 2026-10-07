#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include <astraea/core/result.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::probe {

inline constexpr std::size_t kPs5ProcParamObservationPrefixSize = 16U;

struct Ps5ProcParamObservation {
    astraea::memory::GuestAddress startup_parameters;
    astraea::memory::GuestAddress api_return;

    // Runtime address of one ordinary symbol in the main executable.
    astraea::memory::GuestAddress runtime_anchor;

    // ELF virtual addresses from the exact selected artifact/build.
    std::uint64_t anchor_image_virtual_address = 0;
    std::uint64_t procparam_image_virtual_address = 0;

    // First 16 bytes observed at sceKernelGetProcParam() and the selected
    // artifact's own PT_SCE_PROCPARAM bytes, respectively.
    std::span<const std::byte> api_prefix;
    std::span<const std::byte> artifact_prefix;
};

enum class Ps5ProcParamObservationErrorCode {
    anchor_load_bias_underflow,
    procparam_address_overflow,
    api_prefix_too_small,
    artifact_prefix_too_small,
};

struct Ps5ProcParamObservationError {
    Ps5ProcParamObservationErrorCode code =
        Ps5ProcParamObservationErrorCode::anchor_load_bias_underflow;
    std::size_t expected_size = 0;
    std::size_t actual_size = 0;

    auto operator<=>(const Ps5ProcParamObservationError&) const = default;
};

struct Ps5ProcParamProjection {
    std::uint64_t load_bias = 0;
    astraea::memory::GuestAddress expected_mapped_procparam;
    bool api_return_nonzero = false;
    bool api_matches_expected_mapped_procparam = false;
    bool api_prefix_matches_artifact_prefix = false;
    bool startup_parameters_distinct_from_api_return = false;

    auto operator<=>(const Ps5ProcParamProjection&) const = default;
};

using Ps5ProcParamObservationResult =
    astraea::core::Result<
        Ps5ProcParamProjection,
        Ps5ProcParamObservationError>;

// Validates only relationships supplied by one controlled selected-artifact
// observation. It does not promote PT_SCE_PROCPARAM layout or API semantics
// across SDK/firmware generations.
[[nodiscard]] Ps5ProcParamObservationResult
validate_ps5_procparam_observation(
    const Ps5ProcParamObservation& observation) noexcept;

}  // namespace astraea::probe
