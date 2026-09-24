#pragma once

#include <array>
#include <compare>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::uint32_t kGuestWorkerProtocolVersion = 1U;

struct GuestWorkerId {
    std::uint64_t value = 0;
    auto operator<=>(const GuestWorkerId&) const = default;
};

struct GuestThreadId {
    std::uint64_t value = 0;
    auto operator<=>(const GuestThreadId&) const = default;
};

struct GuestRequestId {
    std::uint64_t value = 0;
    auto operator<=>(const GuestRequestId&) const = default;
};

struct GuestWorkerHello {
    std::uint32_t protocol_version =
        kGuestWorkerProtocolVersion;

    auto operator<=>(const GuestWorkerHello&) const = default;
};

struct GuestWorkerReady {
    std::uint32_t protocol_version =
        kGuestWorkerProtocolVersion;
    GuestWorkerId worker_id;

    auto operator<=>(const GuestWorkerReady&) const = default;
};

struct GuestWorkerRunRequest {
    // Finite controller-owned wall-clock budget for one run request.
    std::uint64_t budget_microseconds = 0;

    auto operator<=>(const GuestWorkerRunRequest&) const = default;
};

struct GuestWorkerSyscallRequest {
    GuestRequestId request_id;
    GuestWorkerId worker_id;
    GuestThreadId thread_id;

    std::uint64_t guest_syscall_number = 0;
    std::array<std::uint64_t, 6> arguments{};
    astraea::memory::GuestAddress guest_rip;

    auto operator<=>(const GuestWorkerSyscallRequest&) const = default;
};

struct GuestWorkerSyscallResult {
    GuestRequestId request_id;
    GuestWorkerId worker_id;
    GuestThreadId thread_id;

    std::int64_t return_value = 0;
    std::int32_t guest_errno = 0;

    auto operator<=>(const GuestWorkerSyscallResult&) const = default;
};

enum class GuestWorkerFaultKind {
    access_violation,
    illegal_instruction,
    unregistered_trap_site,
    protocol_failure,
};

enum class GuestWorkerDiagnosticKind : std::uint32_t {
    loader_rejected = 1U,
    entry_not_executable = 2U,
    sce_dynamic_metadata_rejected = 3U,
    unsupported_dynamic_dependencies = 4U,
    unsupported_relocations = 5U,
    unsupported_tls = 6U,
    native_backend_error = 7U,
    unsupported_syscall = 8U,
    stack_placement_failure = 9U,
};

struct GuestWorkerDiagnostic {
    GuestWorkerId worker_id;
    GuestThreadId thread_id;
    GuestWorkerDiagnosticKind kind =
        GuestWorkerDiagnosticKind::loader_rejected;
    astraea::memory::GuestAddress guest_rip;

    // Kind-specific bounded numeric evidence only. The control protocol
    // deliberately carries no arbitrary strings or host paths.
    std::uint64_t detail0 = 0;
    std::uint64_t detail1 = 0;

    auto operator<=>(const GuestWorkerDiagnostic&) const = default;
};

struct GuestWorkerFault {
    GuestWorkerId worker_id;
    GuestThreadId thread_id;
    GuestWorkerFaultKind kind =
        GuestWorkerFaultKind::protocol_failure;
    astraea::memory::GuestAddress guest_rip;
    astraea::memory::GuestAddress fault_address;

    auto operator<=>(const GuestWorkerFault&) const = default;
};

enum class GuestWorkerStopReason {
    normal_guest_return,
    intercepted_syscall,
    unsupported_syscall,
    guest_fault,
    execution_budget_exhausted,
    controller_termination,
    protocol_failure,
    diagnostic_boundary,
};

struct GuestWorkerStop {
    GuestWorkerId worker_id;
    GuestThreadId thread_id;
    GuestWorkerStopReason reason =
        GuestWorkerStopReason::protocol_failure;
    astraea::memory::GuestAddress guest_rip;

    auto operator<=>(const GuestWorkerStop&) const = default;
};

enum class GuestWorkerTerminationReason {
    user_request,
    execution_budget,
    fatal_guest_fault,
    unsupported_guest_behavior,
    protocol_failure,
};

struct GuestWorkerTerminate {
    GuestWorkerTerminationReason reason =
        GuestWorkerTerminationReason::user_request;

    auto operator<=>(const GuestWorkerTerminate&) const = default;
};

enum class GuestWorkerProtocolValidationErrorCode {
    unsupported_protocol_version,
    zero_run_budget,
    syscall_request_id_mismatch,
    syscall_worker_id_mismatch,
    syscall_thread_id_mismatch,
};

struct GuestWorkerProtocolValidationError {
    GuestWorkerProtocolValidationErrorCode code =
        GuestWorkerProtocolValidationErrorCode::
            unsupported_protocol_version;

    auto operator<=>(const GuestWorkerProtocolValidationError&) const =
        default;
};

using GuestWorkerProtocolValidationResult =
    astraea::core::Result<
        bool,
        GuestWorkerProtocolValidationError>;

[[nodiscard]] GuestWorkerProtocolValidationResult
validate_guest_worker_handshake(
    const GuestWorkerHello& hello,
    const GuestWorkerReady& ready) noexcept;

[[nodiscard]] GuestWorkerProtocolValidationResult
validate_guest_worker_run_request(
    const GuestWorkerRunRequest& request) noexcept;

[[nodiscard]] GuestWorkerProtocolValidationResult
validate_guest_worker_syscall_result(
    const GuestWorkerSyscallRequest& request,
    const GuestWorkerSyscallResult& result) noexcept;

}  // namespace astraea::execution
