#pragma once

#include <compare>

#include <astraea/core/result.hpp>

namespace astraea::execution {

enum class GuestWorkerOwnedFixtureMode {
    normal,
    exit_before_ready,
    invalid_frame_before_ready,
    stall_before_ready,
};

enum class GuestWorkerStdioErrorCode {
    binary_mode_failure,
    input_failure,
    output_failure,
    wire_failure,
    service_failure,
    host_allocation_failure,
};

struct GuestWorkerStdioError {
    GuestWorkerStdioErrorCode code =
        GuestWorkerStdioErrorCode::input_failure;

    auto operator<=>(const GuestWorkerStdioError&) const = default;
};

using GuestWorkerStdioResult =
    astraea::core::Result<
        bool,
        GuestWorkerStdioError>;

// Runs the owned first-proof worker protocol over binary stdin/stdout.
// Human-readable logging must never share these streams.
//
// Fixture modes other than normal exist only for process-supervision tests;
// they never execute guest code.
[[nodiscard]] GuestWorkerStdioResult
run_guest_worker_stdio(
    GuestWorkerOwnedFixtureMode mode =
        GuestWorkerOwnedFixtureMode::normal);

}  // namespace astraea::execution
