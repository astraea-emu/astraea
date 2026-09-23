#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <astraea/core/result.hpp>
#include <astraea/execution/sce_agc_link_shaders.hpp>

namespace astraea::probe {

inline constexpr std::size_t
    kAgcLinkShadersTailCxBytes =
        astraea::execution::
            kSceAgcLinkShadersContextOutputSize;
inline constexpr std::size_t
    kAgcLinkShadersTailUcBytes =
        astraea::execution::
            kSceAgcLinkShadersUserConfigOutputSize;

struct AgcLinkShadersTailObservation {
    std::int64_t link_return_code = 0;
    std::span<const std::byte> cx_raw;
    std::span<const std::byte> uc_raw;
    std::byte cx_sentinel{0};
    std::byte uc_sentinel{0};
};

enum class AgcLinkShadersTailValidationErrorCode {
    link_return_failure,
    cx_size_mismatch,
    uc_size_mismatch,
    interpolant_record_mismatch,
    measured_routing_record_mismatch,
};

struct AgcLinkShadersTailValidationError {
    AgcLinkShadersTailValidationErrorCode code =
        AgcLinkShadersTailValidationErrorCode::
            link_return_failure;
    std::int64_t link_return_code = 0;
    std::size_t expected_size = 0;
    std::size_t actual_size = 0;
    std::optional<std::size_t> record_index;
    std::optional<
        astraea::execution::
            SceAgcLinkShadersRegisterRecord>
        expected_record;
    std::optional<
        astraea::execution::
            SceAgcLinkShadersRegisterRecord>
        actual_record;

    auto operator<=>(
        const AgcLinkShadersTailValidationError&) const = default;
};

struct AgcLinkShadersTailValidatedObservation {
    std::array<std::byte, kAgcLinkShadersTailCxBytes>
        cx_raw{};
    std::array<std::byte, kAgcLinkShadersTailUcBytes>
        uc_raw{};

    astraea::execution::SceAgcLinkShadersRegisterRecord
        unknown_context_record;
    std::array<
        astraea::execution::
            SceAgcLinkShadersRegisterRecord,
        3>
        unknown_user_config_records{};

    bool unknown_context_record_matches_sentinel = false;
    std::array<bool, 3>
        unknown_user_config_record_matches_sentinel{};

    auto operator<=>(
        const AgcLinkShadersTailValidatedObservation&) const = default;
};

using AgcLinkShadersTailValidationResult =
    astraea::core::Result<
        AgcLinkShadersTailValidatedObservation,
        AgcLinkShadersTailValidationError>;

[[nodiscard]] AgcLinkShadersTailValidationResult
validate_agc_link_shaders_tail_observation(
    const AgcLinkShadersTailObservation& observation) noexcept;

enum class AgcLinkShadersTailRepeatDifferenceRegion {
    context,
    user_config,
};

struct AgcLinkShadersTailRepeatDifference {
    AgcLinkShadersTailRepeatDifferenceRegion region =
        AgcLinkShadersTailRepeatDifferenceRegion::context;
    std::size_t byte_offset = 0;
    std::byte first{0};
    std::byte second{0};

    auto operator<=>(
        const AgcLinkShadersTailRepeatDifference&) const = default;
};

struct AgcLinkShadersTailRepeatComparison {
    bool equivalent = true;
    std::optional<AgcLinkShadersTailRepeatDifference>
        first_difference;

    auto operator<=>(
        const AgcLinkShadersTailRepeatComparison&) const = default;
};

// Compares two already validated runs of the same probe case. The comparison
// operates on the complete raw native output blocks, not only the four tail
// records, so promotion cannot hide an upstream change in known output.
[[nodiscard]] AgcLinkShadersTailRepeatComparison
compare_agc_link_shaders_tail_runs(
    const AgcLinkShadersTailValidatedObservation& first,
    const AgcLinkShadersTailValidatedObservation& second) noexcept;

}  // namespace astraea::probe
