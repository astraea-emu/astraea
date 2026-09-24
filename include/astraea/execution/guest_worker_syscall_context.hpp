#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/registered_syscall_trap.hpp>

namespace astraea::execution {

enum class GuestWorkerSyscallContextErrorCode {
    invalid_stop_reason,
    stop_has_gate,
    stop_has_fault,
    zero_request_id,
    zero_worker_id,
    zero_thread_id,
    request_rip_mismatch,
    invalid_registered_trap,
    protocol_validation_failure,
    unsupported_guest_errno,
    resume_rip_overflow,
};

struct GuestWorkerSyscallContextError {
    GuestWorkerSyscallContextErrorCode code =
        GuestWorkerSyscallContextErrorCode::
            invalid_stop_reason;
    std::optional<GuestWorkerProtocolValidationError>
        protocol_error;

    auto operator<=>(const GuestWorkerSyscallContextError&) const =
        default;
};

using GuestWorkerSyscallRequestProjectionResult =
    astraea::core::Result<
        GuestWorkerSyscallRequest,
        GuestWorkerSyscallContextError>;

using GuestWorkerSyscallResumeResult =
    astraea::core::Result<
        GuestCpuContext,
        GuestWorkerSyscallContextError>;

// Projects the first owned C0 synthetic syscall ABI from a native registered
// trap stop into the already-versioned worker protocol.
//
// This is intentionally a fixture contract only, not a claim about the full
// Prospero syscall ABI:
//   number   = RAX
//   args[0]  = RDI
//   args[1]  = RSI
//   args[2]  = RDX
//   args[3]  = R10
//   args[4]  = R8
//   args[5]  = R9
[[nodiscard]] GuestWorkerSyscallRequestProjectionResult
project_registered_syscall_request(
    const ExecutionStop& stop,
    GuestRequestId request_id,
    GuestWorkerId worker_id,
    GuestThreadId thread_id) noexcept;

// Applies only the bounded first-proof resume semantics after validating the
// protocol identity and exact registered trap metadata.
//
// Supported result semantics:
// - guest_errno must be zero;
// - RAX receives the signed synthetic return bit pattern;
// - RIP advances by exactly the registered two-byte SYSCALL length;
// - every other captured register is preserved.
//
// No carry-flag, RCX, R11, errno, TLS, or broader platform syscall behavior is
// inferred here.
[[nodiscard]] GuestWorkerSyscallResumeResult
apply_registered_syscall_result_to_context(
    const ExecutionStop& stop,
    const RegisteredSyscallTrapSite& trap_site,
    const GuestWorkerSyscallRequest& request,
    const GuestWorkerSyscallResult& result) noexcept;

}  // namespace astraea::execution
