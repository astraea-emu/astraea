#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <astraea/core/result.hpp>
#include <astraea/execution/ps5_direct_title_entry.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::probe {

struct Ps5ProcessEntryObservation {
    std::uint64_t rdi = 0;
    std::uint64_t rsi = 0;
    std::uint64_t rbp = 0;
    std::uint64_t rsp = 0;
    std::optional<std::uint64_t> fs_base;
    std::optional<std::uint64_t> gs_base;

    astraea::memory::GuestAddress process_window_base;
    std::span<const std::byte> process_window;
};

enum class Ps5ProcessEntryObservationErrorCode {
    process_window_base_mismatch,
    invalid_direct_title_prefix,
};

struct Ps5ProcessEntryObservationError {
    Ps5ProcessEntryObservationErrorCode code =
        Ps5ProcessEntryObservationErrorCode::
            process_window_base_mismatch;
    std::optional<
        astraea::execution::
            Ps5DirectTitleEntryPrefixError>
        prefix_error;

    auto operator<=>(const Ps5ProcessEntryObservationError&) const =
        default;
};

struct Ps5ProcessEntryStructuralProjection {
    std::uint32_t argc = 0;
    bool argv0_nonzero = false;
    bool rsi_nonzero = false;
    bool rbp_zero = true;
    std::uint8_t rsp_mod16 = 0;
    std::optional<bool> fs_base_nonzero;
    std::optional<bool> gs_base_nonzero;

    auto operator<=>(const Ps5ProcessEntryStructuralProjection&) const =
        default;
};

struct Ps5ProcessEntryValidatedObservation {
    astraea::execution::Ps5DirectTitleEntryPrefix
        direct_title_prefix;

    // Raw register observations are preserved exactly. They are evidence, not
    // required values. Cross-run equality is assessed through the structural
    // projection below rather than by assuming raw addresses are stable.
    std::uint64_t rsi = 0;
    std::uint64_t rbp = 0;
    std::uint64_t rsp = 0;
    std::optional<std::uint64_t> fs_base;
    std::optional<std::uint64_t> gs_base;

    Ps5ProcessEntryStructuralProjection projection;

    auto operator<=>(const Ps5ProcessEntryValidatedObservation&) const =
        default;
};

using Ps5ProcessEntryObservationValidationResult =
    astraea::core::Result<
        Ps5ProcessEntryValidatedObservation,
        Ps5ProcessEntryObservationError>;

[[nodiscard]] Ps5ProcessEntryObservationValidationResult
validate_ps5_process_entry_observation(
    const Ps5ProcessEntryObservation& observation) noexcept;

enum class Ps5ProcessEntryProjectionField {
    argc,
    argv0_nonzero,
    rsi_nonzero,
    rbp_zero,
    rsp_mod16,
    fs_base_nonzero,
    gs_base_nonzero,
};

struct Ps5ProcessEntryProjectionDifference {
    Ps5ProcessEntryProjectionField field =
        Ps5ProcessEntryProjectionField::argc;

    auto operator<=>(const Ps5ProcessEntryProjectionDifference&) const =
        default;
};

struct Ps5ProcessEntryRepeatComparison {
    bool equivalent = true;
    std::optional<Ps5ProcessEntryProjectionDifference>
        first_difference;

    auto operator<=>(const Ps5ProcessEntryRepeatComparison&) const =
        default;
};

// Compares only structural facts that the probe definition declares stable
// across equivalent cases. Raw pointers/register values remain available in
// each validated observation for evidence review but are not normalized into
// guessed constants merely to make two runs compare equal.
[[nodiscard]] Ps5ProcessEntryRepeatComparison
compare_ps5_process_entry_observations(
    const Ps5ProcessEntryValidatedObservation& first,
    const Ps5ProcessEntryValidatedObservation& second) noexcept;

}  // namespace astraea::probe
