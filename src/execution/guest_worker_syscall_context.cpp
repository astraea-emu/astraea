#include <astraea/execution/guest_worker_syscall_context.hpp>

#include <bit>
#include <limits>

namespace astraea::execution {
namespace {

[[nodiscard]] GuestWorkerSyscallContextError make_error(
    GuestWorkerSyscallContextErrorCode code,
    std::optional<GuestWorkerProtocolValidationError>
        protocol_error = std::nullopt) noexcept {
    return GuestWorkerSyscallContextError{
        .code = code,
        .protocol_error = protocol_error,
    };
}

[[nodiscard]] bool valid_registered_stop(
    const ExecutionStop& stop,
    GuestWorkerSyscallContextError& error) noexcept {
    if (stop.reason !=
        ExecutionStopReason::registered_syscall_trap) {
        error = make_error(
            GuestWorkerSyscallContextErrorCode::
                invalid_stop_reason);
        return false;
    }
    if (stop.has_gate_slot) {
        error = make_error(
            GuestWorkerSyscallContextErrorCode::
                stop_has_gate);
        return false;
    }
    if (stop.has_fault) {
        error = make_error(
            GuestWorkerSyscallContextErrorCode::
                stop_has_fault);
        return false;
    }
    return true;
}

}  // namespace

GuestWorkerSyscallRequestProjectionResult
project_registered_syscall_request(
    const ExecutionStop& stop,
    GuestRequestId request_id,
    GuestWorkerId worker_id,
    GuestThreadId thread_id) noexcept {
    GuestWorkerSyscallContextError stop_error{};
    if (!valid_registered_stop(
            stop,
            stop_error)) {
        return GuestWorkerSyscallRequestProjectionResult::failure(
            stop_error);
    }

    if (request_id.value == 0U) {
        return GuestWorkerSyscallRequestProjectionResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    zero_request_id));
    }
    if (worker_id.value == 0U) {
        return GuestWorkerSyscallRequestProjectionResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    zero_worker_id));
    }
    if (thread_id.value == 0U) {
        return GuestWorkerSyscallRequestProjectionResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    zero_thread_id));
    }

    const auto& context = stop.context;
    return GuestWorkerSyscallRequestProjectionResult::success(
        GuestWorkerSyscallRequest{
            .request_id = request_id,
            .worker_id = worker_id,
            .thread_id = thread_id,
            .guest_syscall_number = context.rax,
            .arguments = {
                context.rdi,
                context.rsi,
                context.rdx,
                context.r10,
                context.r8,
                context.r9,
            },
            .guest_rip =
                astraea::memory::GuestAddress{
                    context.rip},
        });
}

GuestWorkerSyscallResumeResult
apply_registered_syscall_result_to_context(
    const ExecutionStop& stop,
    const RegisteredSyscallTrapSite& trap_site,
    const GuestWorkerSyscallRequest& request,
    const GuestWorkerSyscallResult& result) noexcept {
    GuestWorkerSyscallContextError stop_error{};
    if (!valid_registered_stop(
            stop,
            stop_error)) {
        return GuestWorkerSyscallResumeResult::failure(
            stop_error);
    }

    if (request.guest_rip.value() !=
        stop.context.rip) {
        return GuestWorkerSyscallResumeResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    request_rip_mismatch));
    }

    if (trap_site.original_bytes !=
            kX86SyscallBytes ||
        trap_site.trap_bytes !=
            kX86Ud2Bytes ||
        !registered_syscall_trap_matches_rip(
            trap_site,
            request.guest_rip)) {
        return GuestWorkerSyscallResumeResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    invalid_registered_trap));
    }

    const auto validated =
        validate_guest_worker_syscall_result(
            request,
            result);
    if (!validated.has_value()) {
        return GuestWorkerSyscallResumeResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    protocol_validation_failure,
                validated.error()));
    }

    if (result.guest_errno != 0) {
        return GuestWorkerSyscallResumeResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    unsupported_guest_errno));
    }

    constexpr auto kInstructionLength =
        static_cast<std::uint64_t>(
            kX86SyscallBytes.size());
    if (stop.context.rip >
        std::numeric_limits<std::uint64_t>::max() -
            kInstructionLength) {
        return GuestWorkerSyscallResumeResult::failure(
            make_error(
                GuestWorkerSyscallContextErrorCode::
                    resume_rip_overflow));
    }

    auto resumed = stop.context;
    resumed.rax =
        std::bit_cast<std::uint64_t>(
            result.return_value);
    resumed.rip += kInstructionLength;

    return GuestWorkerSyscallResumeResult::success(
        resumed);
}

}  // namespace astraea::execution
