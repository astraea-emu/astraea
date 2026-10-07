#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include <astraea/core/result.hpp>
#include <astraea/loader/elf64.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::probe {

inline constexpr std::size_t kPs5ProcParamObservationPrefixSize = 16U;

struct Ps5ProcParamArtifactEvidence {
    std::uint64_t entry_image_virtual_address = 0;
    std::uint64_t procparam_image_virtual_address = 0;
    std::array<std::byte, kPs5ProcParamObservationPrefixSize> procparam_prefix{};

    auto operator<=>(const Ps5ProcParamArtifactEvidence&) const = default;
};

enum class Ps5ProcParamArtifactErrorCode {
    missing_process_parameter_segment,
    multiple_process_parameter_segments,
    process_parameter_segment_too_small,
    process_parameter_prefix_out_of_bounds,
};

struct Ps5ProcParamArtifactError {
    Ps5ProcParamArtifactErrorCode code =
        Ps5ProcParamArtifactErrorCode::missing_process_parameter_segment;
    std::size_t program_header_index = 0;

    auto operator<=>(const Ps5ProcParamArtifactError&) const = default;
};

using Ps5ProcParamArtifactResult =
    astraea::core::Result<
        Ps5ProcParamArtifactEvidence,
        Ps5ProcParamArtifactError>;

// Extracts only selected-artifact structural evidence. This does not assign
// any runtime meaning to PT_SCE_PROCPARAM.
[[nodiscard]] Ps5ProcParamArtifactResult
extract_ps5_procparam_artifact_evidence(
    const astraea::loader::ElfImage& image,
    std::span<const std::byte> artifact_bytes) noexcept;

struct Ps5ProcParamObservation {
    astraea::memory::GuestAddress startup_parameters;
    astraea::memory::GuestAddress api_return;

    // Runtime address of this exact title's entry observer. The corresponding
    // image-relative value is artifact.entry_image_virtual_address.
    astraea::memory::GuestAddress entry_runtime_address;

    // Empty when api_return is zero; otherwise at least 16 bytes.
    std::span<const std::byte> api_prefix;
};

enum class Ps5ProcParamObservationErrorCode {
    entry_load_bias_underflow,
    procparam_address_overflow,
    api_prefix_too_small,
};

struct Ps5ProcParamObservationError {
    Ps5ProcParamObservationErrorCode code =
        Ps5ProcParamObservationErrorCode::entry_load_bias_underflow;
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

// Validates only relationships for one exact controlled artifact. It does not
// promote procparam/API semantics across SDK or firmware generations.
[[nodiscard]] Ps5ProcParamObservationResult
validate_ps5_procparam_observation(
    const Ps5ProcParamArtifactEvidence& artifact,
    const Ps5ProcParamObservation& observation) noexcept;

}  // namespace astraea::probe
