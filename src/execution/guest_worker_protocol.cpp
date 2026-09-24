#include <astraea/execution/guest_worker_protocol.hpp>

namespace astraea::execution {
namespace {

[[nodiscard]] GuestWorkerProtocolValidationError error(
    GuestWorkerProtocolValidationErrorCode code) noexcept {
    return GuestWorkerProtocolValidationError{
        .code = code,
    };
}

}  // namespace

GuestWorkerProtocolValidationResult
validate_guest_worker_handshake(
    const GuestWorkerHello& hello,
    const GuestWorkerReady& ready) noexcept {
    if (hello.protocol_version !=
            kGuestWorkerProtocolVersion ||
        ready.protocol_version !=
            kGuestWorkerProtocolVersion ||
        hello.protocol_version !=
            ready.protocol_version) {
        return GuestWorkerProtocolValidationResult::failure(
            error(
                GuestWorkerProtocolValidationErrorCode::
                    unsupported_protocol_version));
    }

    return GuestWorkerProtocolValidationResult::success(
        true);
}

GuestWorkerProtocolValidationResult
validate_guest_worker_run_request(
    const GuestWorkerRunRequest& request) noexcept {
    if (request.budget_microseconds == 0U) {
        return GuestWorkerProtocolValidationResult::failure(
            error(
                GuestWorkerProtocolValidationErrorCode::
                    zero_run_budget));
    }

    return GuestWorkerProtocolValidationResult::success(
        true);
}

GuestWorkerProtocolValidationResult
validate_guest_worker_syscall_result(
    const GuestWorkerSyscallRequest& request,
    const GuestWorkerSyscallResult& result) noexcept {
    if (request.request_id != result.request_id) {
        return GuestWorkerProtocolValidationResult::failure(
            error(
                GuestWorkerProtocolValidationErrorCode::
                    syscall_request_id_mismatch));
    }

    if (request.worker_id != result.worker_id) {
        return GuestWorkerProtocolValidationResult::failure(
            error(
                GuestWorkerProtocolValidationErrorCode::
                    syscall_worker_id_mismatch));
    }

    if (request.thread_id != result.thread_id) {
        return GuestWorkerProtocolValidationResult::failure(
            error(
                GuestWorkerProtocolValidationErrorCode::
                    syscall_thread_id_mismatch));
    }

    return GuestWorkerProtocolValidationResult::success(
        true);
}

}  // namespace astraea::execution
