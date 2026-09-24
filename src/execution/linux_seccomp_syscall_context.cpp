#include <astraea/execution/linux_seccomp_syscall_context.hpp>

#include <array>
#include <bit>
#include <limits>

namespace astraea::execution {
namespace {

[[nodiscard]] LinuxSeccompSyscallContextError make_error(
    LinuxSeccompSyscallContextErrorCode code,
    std::optional<GuestWorkerProtocolValidationError>
        protocol_error = std::nullopt) noexcept {
    return LinuxSeccompSyscallContextError{
        .code = code,
        .protocol_error = protocol_error,
    };
}

[[nodiscard]] bool
validate_x86_64_seccomp_trap(
    const LinuxSeccompSyscallTrap& trap,
    LinuxSeccompSyscallContextError& error) noexcept {
    if (trap.audit_arch != kLinuxAuditArchX86_64) {
        error = make_error(
            LinuxSeccompSyscallContextErrorCode::
                unsupported_audit_arch);
        return false;
    }

    if (trap.syscall_number < 0) {
        error = make_error(
            LinuxSeccompSyscallContextErrorCode::
                unsupported_syscall_number);
        return false;
    }

    constexpr std::uint64_t kSyscallLength = 2U;
    if (trap.guest_rip.value() >
        std::numeric_limits<std::uint64_t>::max() -
            kSyscallLength) {
        error = make_error(
            LinuxSeccompSyscallContextErrorCode::
                call_rip_overflow);
        return false;
    }

    if (trap.context.rip !=
        trap.guest_rip.value() + kSyscallLength) {
        error = make_error(
            LinuxSeccompSyscallContextErrorCode::
                post_syscall_rip_mismatch);
        return false;
    }

    return true;
}

[[nodiscard]] std::array<std::uint64_t, 6>
project_arguments(
    const GuestCpuContext& context) noexcept {
    return {
        context.rdi,
        context.rsi,
        context.rdx,
        context.r10,
        context.r8,
        context.r9,
    };
}

}  // namespace

LinuxSeccompSyscallRequestProjectionResult
project_linux_seccomp_syscall_request(
    const LinuxSeccompSyscallTrap& trap,
    GuestRequestId request_id,
    GuestWorkerId worker_id,
    GuestThreadId thread_id) noexcept {
    LinuxSeccompSyscallContextError trap_error{};
    if (!validate_x86_64_seccomp_trap(
            trap,
            trap_error)) {
        return LinuxSeccompSyscallRequestProjectionResult::failure(
            trap_error);
    }

    if (request_id.value == 0U) {
        return LinuxSeccompSyscallRequestProjectionResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    zero_request_id));
    }
    if (worker_id.value == 0U) {
        return LinuxSeccompSyscallRequestProjectionResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    zero_worker_id));
    }
    if (thread_id.value == 0U) {
        return LinuxSeccompSyscallRequestProjectionResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    zero_thread_id));
    }

    return LinuxSeccompSyscallRequestProjectionResult::success(
        GuestWorkerSyscallRequest{
            .request_id = request_id,
            .worker_id = worker_id,
            .thread_id = thread_id,
            .guest_syscall_number =
                static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(
                        trap.syscall_number)),
            .arguments =
                project_arguments(trap.context),
            .guest_rip = trap.guest_rip,
        });
}

LinuxSeccompSyscallResumeResult
apply_linux_seccomp_syscall_result_to_context(
    const LinuxSeccompSyscallTrap& trap,
    const GuestWorkerSyscallRequest& request,
    const GuestWorkerSyscallResult& result) noexcept {
    LinuxSeccompSyscallContextError trap_error{};
    if (!validate_x86_64_seccomp_trap(
            trap,
            trap_error)) {
        return LinuxSeccompSyscallResumeResult::failure(
            trap_error);
    }

    if (request.guest_rip != trap.guest_rip) {
        return LinuxSeccompSyscallResumeResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    request_rip_mismatch));
    }

    const auto expected_number =
        static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(
                trap.syscall_number));
    if (request.guest_syscall_number !=
        expected_number) {
        return LinuxSeccompSyscallResumeResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    request_syscall_mismatch));
    }

    if (request.arguments !=
        project_arguments(trap.context)) {
        return LinuxSeccompSyscallResumeResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    request_argument_mismatch));
    }

    const auto validated =
        validate_guest_worker_syscall_result(
            request,
            result);
    if (!validated.has_value()) {
        return LinuxSeccompSyscallResumeResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    protocol_validation_failure,
                validated.error()));
    }

    if (result.guest_errno != 0) {
        return LinuxSeccompSyscallResumeResult::failure(
            make_error(
                LinuxSeccompSyscallContextErrorCode::
                    unsupported_guest_errno));
    }

    auto resumed = trap.context;
    resumed.rax =
        std::bit_cast<std::uint64_t>(
            result.return_value);

    return LinuxSeccompSyscallResumeResult::success(
        resumed);
}

}  // namespace astraea::execution
