#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/guest_worker_stdio.hpp>
#include <astraea/execution/guest_worker_wire.hpp>

namespace astraea::execution {

enum class GuestWorkerProcessErrorCode {
    backend_unavailable,
    invalid_run_budget,
    invalid_timeout,
    pipe_creation_failure,
    spawn_failure,
    write_failure,
    read_failure,
    wire_failure,
    handshake_failure,
    unexpected_response,
    worker_exited_early,
    execution_timeout,
    termination_failure,
    wait_failure,
    worker_exit_failure,
    host_allocation_failure,
};

struct GuestWorkerProcessError {
    GuestWorkerProcessErrorCode code =
        GuestWorkerProcessErrorCode::backend_unavailable;
    std::int64_t native_error = 0;
    std::optional<GuestWorkerWireError> wire_error;
    std::optional<GuestWorkerProtocolValidationError>
        protocol_error;

    auto operator<=>(const GuestWorkerProcessError&) const = default;
};

struct GuestWorkerProcessProof {
    GuestWorkerId worker_id;
    GuestWorkerStop stop;
    std::int32_t worker_exit_code = 0;
    bool forced_termination = false;

    auto operator<=>(const GuestWorkerProcessProof&) const = default;
};

using GuestWorkerProcessProofResult =
    astraea::core::Result<
        GuestWorkerProcessProof,
        GuestWorkerProcessError>;

[[nodiscard]] bool
guest_worker_process_proof_available() noexcept;

// Launches the owned worker executable in a separate OS process, performs
// HELLO -> READY -> RUN_REQUEST -> STOP -> TERMINATE, and waits for exact
// child teardown under a controller-owned timeout.
//
// No guest instructions execute in this proof. Non-normal fixture modes exist
// only to verify crash/malformed-frame/timeout containment.
[[nodiscard]] GuestWorkerProcessProofResult
run_guest_worker_process_proof(
    const std::filesystem::path& worker_executable,
    std::uint64_t run_budget_microseconds,
    std::uint64_t timeout_milliseconds,
    GuestWorkerOwnedFixtureMode fixture_mode =
        GuestWorkerOwnedFixtureMode::normal);

}  // namespace astraea::execution
