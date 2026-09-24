#pragma once

#include <compare>

#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>

namespace astraea::execution {

enum class GuestWorkerFaultProjectionErrorCode {
    invalid_stop_reason,
    missing_fault,
    fault_rip_mismatch,
    unsupported_fault_kind,
    zero_worker_id,
    zero_thread_id,
};

struct GuestWorkerFaultProjectionError {
    GuestWorkerFaultProjectionErrorCode code =
        GuestWorkerFaultProjectionErrorCode::
            invalid_stop_reason;

    auto operator<=>(const GuestWorkerFaultProjectionError&) const =
        default;
};

using GuestWorkerFaultProjectionResult =
    astraea::core::Result<
        GuestWorkerFault,
        GuestWorkerFaultProjectionError>;

// Projects only already-verified terminal native CPU fault classes into the
// bounded worker protocol. This runs after the platform handler has unwound
// into ordinary worker code; no signal/VEH handler performs allocation or IPC.
//
// First C0 mapping:
// - access_violation -> access_violation
// - illegal_instruction -> illegal_instruction
//
// Arithmetic/trap/unknown classes remain unsupported in this slice rather than
// being silently generalized.
[[nodiscard]] GuestWorkerFaultProjectionResult
project_guest_worker_fault(
    const ExecutionStop& stop,
    GuestWorkerId worker_id,
    GuestThreadId thread_id) noexcept;

}  // namespace astraea::execution
