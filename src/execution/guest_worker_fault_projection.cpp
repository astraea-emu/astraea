#include <astraea/execution/guest_worker_fault_projection.hpp>

namespace astraea::execution {
namespace {

[[nodiscard]] GuestWorkerFaultProjectionError make_error(
    GuestWorkerFaultProjectionErrorCode code) noexcept {
    return GuestWorkerFaultProjectionError{
        .code = code,
    };
}

}  // namespace

GuestWorkerFaultProjectionResult
project_guest_worker_fault(
    const ExecutionStop& stop,
    GuestWorkerId worker_id,
    GuestThreadId thread_id) noexcept {
    if (stop.reason != ExecutionStopReason::guest_fault) {
        return GuestWorkerFaultProjectionResult::failure(
            make_error(
                GuestWorkerFaultProjectionErrorCode::
                    invalid_stop_reason));
    }
    if (!stop.has_fault) {
        return GuestWorkerFaultProjectionResult::failure(
            make_error(
                GuestWorkerFaultProjectionErrorCode::
                    missing_fault));
    }
    if (stop.fault.instruction_pointer !=
        stop.context.rip) {
        return GuestWorkerFaultProjectionResult::failure(
            make_error(
                GuestWorkerFaultProjectionErrorCode::
                    fault_rip_mismatch));
    }
    if (worker_id.value == 0U) {
        return GuestWorkerFaultProjectionResult::failure(
            make_error(
                GuestWorkerFaultProjectionErrorCode::
                    zero_worker_id));
    }
    if (thread_id.value == 0U) {
        return GuestWorkerFaultProjectionResult::failure(
            make_error(
                GuestWorkerFaultProjectionErrorCode::
                    zero_thread_id));
    }

    GuestWorkerFaultKind kind{};
    switch (stop.fault.kind) {
    case GuestFaultKind::access_violation:
        kind = GuestWorkerFaultKind::access_violation;
        break;
    case GuestFaultKind::illegal_instruction:
        kind = GuestWorkerFaultKind::illegal_instruction;
        break;
    case GuestFaultKind::arithmetic:
    case GuestFaultKind::breakpoint_or_trap:
    case GuestFaultKind::unknown:
        return GuestWorkerFaultProjectionResult::failure(
            make_error(
                GuestWorkerFaultProjectionErrorCode::
                    unsupported_fault_kind));
    }

    return GuestWorkerFaultProjectionResult::success(
        GuestWorkerFault{
            .worker_id = worker_id,
            .thread_id = thread_id,
            .kind = kind,
            .guest_rip =
                astraea::memory::GuestAddress{
                    stop.fault.instruction_pointer},
            .fault_address =
                astraea::memory::GuestAddress{
                    stop.fault.has_fault_address
                        ? stop.fault.fault_address
                        : 0U},
        });
}

}  // namespace astraea::execution
