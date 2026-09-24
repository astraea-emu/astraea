#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/guest_worker_wire.hpp>

namespace astraea::execution {

using GuestWorkerSyscallService =
    std::function<
        std::optional<GuestWorkerSyscallResult>(
            const GuestWorkerSyscallRequest&)>;

struct GuestWorkerProcessSessionConfig {
    std::string worker_executable;
    std::vector<std::string> worker_arguments;
    std::uint64_t run_budget_microseconds = 0;
    std::uint64_t timeout_milliseconds = 0;

    // Optional ordinary-controller callback for typed syscall mediation.
    // A non-empty service requires a finite non-zero request limit. No
    // callback is ever invoked from signal/exception-handler context.
    GuestWorkerSyscallService syscall_service;
    std::size_t max_syscall_requests = 0;
};

struct GuestWorkerProcessSessionResult {
    GuestWorkerReady ready;
    GuestWorkerStop stop;
    std::int32_t child_exit_code = 0;
    std::size_t syscall_request_count = 0;

    auto operator<=>(const GuestWorkerProcessSessionResult&) const =
        default;
};

enum class GuestWorkerProcessSessionErrorCode {
    unsupported_platform,
    invalid_config,
    channel_creation_failed,
    spawn_failed,
    io_failure,
    timeout,
    unexpected_eof,
    wire_failure,
    unexpected_message,
    protocol_failure,
    worker_identity_mismatch,
    syscall_service_unavailable,
    syscall_service_rejected,
    syscall_request_limit_exceeded,
    child_exit_failure,
    host_allocation_failure,
};

struct GuestWorkerProcessSessionError {
    GuestWorkerProcessSessionErrorCode code =
        GuestWorkerProcessSessionErrorCode::invalid_config;
    std::int64_t platform_error = 0;
    std::optional<GuestWorkerWireError> wire_error;
    std::optional<GuestWorkerProtocolValidationError>
        protocol_error;

    auto operator<=>(const GuestWorkerProcessSessionError&) const =
        default;
};

using GuestWorkerProcessSessionRunResult =
    astraea::core::Result<
        GuestWorkerProcessSessionResult,
        GuestWorkerProcessSessionError>;

[[nodiscard]] bool
guest_worker_process_session_available() noexcept;

// First supervised-process proof.
//
// The controller launches one owned worker executable with a deliberately
// minimal inherited-resource set, exchanges:
//
//   HELLO -> READY -> RUN_REQUEST
//       -> zero or more bounded SYSCALL_REQUEST / SYSCALL_RESULT exchanges
//       -> STOP -> TERMINATE
//
// and returns only after the child exits and is reaped. The run budget and
// controller timeout are finite. Any timeout/protocol/I/O failure tears the
// worker down before returning.
//
// This function performs no guest instruction execution or trap handling.
// When explicitly configured, it may broker bounded typed syscall messages in
// ordinary controller code. It performs no filesystem/network brokering or
// retail loading.
[[nodiscard]] GuestWorkerProcessSessionRunResult
run_guest_worker_process_session(
    const GuestWorkerProcessSessionConfig& config);

}  // namespace astraea::execution
