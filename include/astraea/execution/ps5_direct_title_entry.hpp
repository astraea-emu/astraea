#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include <astraea/core/result.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

// Independently corroborated direct-title prefix only.
//
// Current evidence supports:
//   RDI -> process-parameter block
//   +0  -> argc-like 32-bit field
//   +8  -> argv-like pointer vector
//
// Nothing after argv[0] is interpreted by this slice. In particular, this
// contract does not establish the total parameter-block extent, environment,
// auxv-like state, loader teardown semantics, initial RSP, TLS/TCB, or
// bootstrap ordering.
inline constexpr std::size_t
    kPs5DirectTitleEntryPrefixSize = 16U;
inline constexpr std::uint64_t
    kPs5DirectTitleArgcOffset = 0U;
inline constexpr std::uint64_t
    kPs5DirectTitleArgvOffset = 8U;

struct Ps5DirectTitleEntryPrefixObservation {
    astraea::memory::GuestAddress process_parameters;
    std::span<const std::byte> process_prefix;
};

enum class Ps5DirectTitleEntryPrefixErrorCode {
    zero_process_parameter_address,
    prefix_too_small,
    argv_address_overflow,
};

struct Ps5DirectTitleEntryPrefixError {
    Ps5DirectTitleEntryPrefixErrorCode code =
        Ps5DirectTitleEntryPrefixErrorCode::
            zero_process_parameter_address;
    std::size_t expected_size = 0;
    std::size_t actual_size = 0;

    auto operator<=>(const Ps5DirectTitleEntryPrefixError&) const =
        default;
};

struct Ps5DirectTitleEntryPrefix {
    astraea::memory::GuestAddress process_parameters;
    std::uint32_t argc = 0;

    // Guest address of the first argv pointer slot. This is process_parameters
    // + 8 for the corroborated profile.
    astraea::memory::GuestAddress argv;

    // Opaque observation decoded from argv[0]. Zero is not rejected because
    // this slice does not invent argc/argv validity rules beyond the observed
    // offsets.
    astraea::memory::GuestAddress first_argv_pointer;

    auto operator<=>(const Ps5DirectTitleEntryPrefix&) const = default;
};

using Ps5DirectTitleEntryPrefixResult =
    astraea::core::Result<
        Ps5DirectTitleEntryPrefix,
        Ps5DirectTitleEntryPrefixError>;

[[nodiscard]] Ps5DirectTitleEntryPrefixResult
validate_ps5_direct_title_entry_prefix(
    const Ps5DirectTitleEntryPrefixObservation& observation) noexcept;

enum class Ps5DirectTitleEntryBlocker {
    loader_teardown_contract,
    initial_rsp_contract,
    primary_thread_tls_contract,
    bootstrap_contract,
};

struct Ps5DirectTitleEntryReadinessRequest {
    bool loader_teardown_contract_established = false;
    bool initial_rsp_contract_established = false;
    bool primary_thread_tls_contract_established = false;
    bool bootstrap_contract_established = false;

    auto operator<=>(const Ps5DirectTitleEntryReadinessRequest&) const =
        default;
};

struct Ps5DirectTitleEntryReadiness {
    std::array<Ps5DirectTitleEntryBlocker, 4> blockers{};
    std::size_t blocker_count = 0;

    [[nodiscard]] bool ready() const noexcept {
        return blocker_count == 0U;
    }

    auto operator<=>(const Ps5DirectTitleEntryReadiness&) const =
        default;
};

// This is intentionally separate from prefix validation. A valid corroborated
// prefix is not permission to execute retail code. Native entry remains
// blocked until every load-bearing contract required by the selected profile
// is explicitly established.
[[nodiscard]] Ps5DirectTitleEntryReadiness
assess_ps5_direct_title_entry_readiness(
    const Ps5DirectTitleEntryReadinessRequest& request) noexcept;

}  // namespace astraea::execution
