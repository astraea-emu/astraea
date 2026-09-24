#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/linux_execution.hpp>

namespace astraea::execution {

// Linux UAPI AUDIT_ARCH_X86_64. Kept local so the portable helper can compile
// on non-Linux CI hosts while remaining explicit about the only ABI admitted
// by the first resume proof.
inline constexpr std::uint32_t kLinuxAuditArchX86_64 =
    0xc000003eU;

enum class LinuxSeccompSyscallContextErrorCode {
    zero_request_id,
    zero_worker_id,
    zero_thread_id,
    unsupported_audit_arch,
    unsupported_syscall_number,
    call_rip_overflow,
    post_syscall_rip_mismatch,
    request_rip_mismatch,
    request_syscall_mismatch,
    request_argument_mismatch,
    protocol_validation_failure,
    unsupported_guest_errno,
};

struct LinuxSeccompSyscallContextError {
    LinuxSeccompSyscallContextErrorCode code =
        LinuxSeccompSyscallContextErrorCode::
            unsupported_audit_arch;
    std::optional<GuestWorkerProtocolValidationError>
        protocol_error;

    auto operator<=>(const LinuxSeccompSyscallContextError&) const =
        default;
};

using LinuxSeccompSyscallRequestProjectionResult =
    astraea::core::Result<
        GuestWorkerSyscallRequest,
        LinuxSeccompSyscallContextError>;

using LinuxSeccompSyscallResumeResult =
    astraea::core::Result<
        GuestCpuContext,
        LinuxSeccompSyscallContextError>;

// First Linux x86-64 seccomp projection profile only.
//
// number  = kernel-reported si_syscall
// args    = RDI, RSI, RDX, R10, R8, R9
// guest_rip = original kernel-reported syscall call site
//
// The captured processor RIP must already be guest_rip + 2 for the x86-64
// SYSCALL instruction. This is intentionally distinct from the registered-UD2
// path, whose captured RIP still points at the substituted instruction.
[[nodiscard]] LinuxSeccompSyscallRequestProjectionResult
project_linux_seccomp_syscall_request(
    const LinuxSeccompSyscallTrap& trap,
    GuestRequestId request_id,
    GuestWorkerId worker_id,
    GuestThreadId thread_id) noexcept;

// Applies one validated synthetic controller result. Because seccomp SIGSYS
// already captures post-SYSCALL PC, successful resume changes only RAX.
// Every other captured register, including RIP/RCX/R11/RFLAGS, is preserved.
[[nodiscard]] LinuxSeccompSyscallResumeResult
apply_linux_seccomp_syscall_result_to_context(
    const LinuxSeccompSyscallTrap& trap,
    const GuestWorkerSyscallRequest& request,
    const GuestWorkerSyscallResult& result) noexcept;

}  // namespace astraea::execution
