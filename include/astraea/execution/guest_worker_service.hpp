#pragma once

#include <compare>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_worker_wire.hpp>

namespace astraea::execution {

inline constexpr GuestWorkerId kOwnedGuestWorkerProofId{
    .value = 1U,
};
inline constexpr GuestThreadId kOwnedGuestWorkerProofThreadId{
    .value = 1U,
};

enum class GuestWorkerServicePhase {
    awaiting_hello,
    ready,
    terminated,
};

struct GuestWorkerServiceState {
    GuestWorkerServicePhase phase =
        GuestWorkerServicePhase::awaiting_hello;

    auto operator<=>(const GuestWorkerServiceState&) const = default;
};

struct GuestWorkerServiceTransition {
    std::optional<GuestWorkerWireMessage> response;
    bool terminate_process = false;

    auto operator<=>(const GuestWorkerServiceTransition&) const = default;
};

enum class GuestWorkerServiceErrorCode {
    unexpected_message,
    unsupported_protocol_version,
    invalid_run_request,
    already_terminated,
};

struct GuestWorkerServiceError {
    GuestWorkerServiceErrorCode code =
        GuestWorkerServiceErrorCode::unexpected_message;

    auto operator<=>(const GuestWorkerServiceError&) const = default;
};

using GuestWorkerServiceResult =
    astraea::core::Result<
        GuestWorkerServiceTransition,
        GuestWorkerServiceError>;

// Portable first-proof worker state machine. It performs no guest execution.
// The only valid sequence is:
//
//   HELLO -> READY
//   RUN_REQUEST -> STOP(normal_guest_return)
//   TERMINATE -> process exit
//
// This exists so process/pipe code never owns protocol sequencing semantics.
[[nodiscard]] GuestWorkerServiceResult
handle_guest_worker_service_message(
    GuestWorkerServiceState& state,
    const GuestWorkerWireMessage& message) noexcept;

}  // namespace astraea::execution
