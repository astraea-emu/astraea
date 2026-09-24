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

struct GuestWorkerResourcePolicy {
    // Common cross-platform ceilings. Linux interprets memory as RLIMIT_AS;
    // Windows interprets it as the Job Object per-process committed-memory
    // limit. These are containment ceilings, not emulated PS5 hardware sizes.
    std::optional<std::uint64_t> process_memory_limit_bytes;
    std::optional<std::uint64_t> process_cpu_time_seconds;

    // Linux-only limits. Requesting these on another platform is invalid
    // rather than silently weakening the requested policy.
    std::optional<std::uint64_t> linux_max_open_files;
    bool linux_disable_core_dumps = false;
    bool linux_disable_file_growth = false;

    auto operator<=>(const GuestWorkerResourcePolicy&) const = default;
};

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

    // Optional kernel-enforced worker ceilings installed before guest RUN.
    std::optional<GuestWorkerResourcePolicy> resource_policy;

    // Linux-only immutable artifact handoff. The controller supplies bytes,
    // never a host pathname. When present, launch seals the bytes into an
    // anonymous memfd and exposes only that object as child fd 3. The worker
    // is expected to copy/validate and close fd 3 before guest RUN.
    std::optional<std::vector<std::byte>> linux_artifact_bytes;
};

struct GuestWorkerProcessSessionResult {
    GuestWorkerReady ready;
    GuestWorkerStop stop;
    std::int32_t child_exit_code = 0;
    std::size_t syscall_request_count = 0;
    std::optional<GuestWorkerFault> terminal_fault;
    std::optional<GuestWorkerDiagnostic> terminal_diagnostic;

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
    resource_policy_failure,
    artifact_preparation_failure,
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
//       -> optionally one terminal FAULT or DIAGNOSTIC
//       -> matching STOP -> TERMINATE
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
