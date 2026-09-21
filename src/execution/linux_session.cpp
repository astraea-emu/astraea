#include <astraea/execution/linux_session.hpp>

#include <cstdint>
#include <utility>

#include <astraea/execution/guest_memory.hpp>

namespace astraea::execution {
namespace {

[[nodiscard]] LinuxSyntheticSessionError backend_session_error(
    NativeBackendError error) noexcept {
    return LinuxSyntheticSessionError{
        .kind =
            LinuxSyntheticSessionErrorKind::backend,
        .backend_error = error,
        .hle_error = {},
        .guest_fault = {},
    };
}

[[nodiscard]] LinuxSyntheticSessionError hle_session_error(
    HleRuntimeError error) noexcept {
    return LinuxSyntheticSessionError{
        .kind =
            LinuxSyntheticSessionErrorKind::hle,
        .backend_error = {},
        .hle_error = error,
        .guest_fault = {},
    };
}

[[nodiscard]] LinuxSyntheticSessionError fault_session_error(
    GuestFault fault) noexcept {
    return LinuxSyntheticSessionError{
        .kind =
            LinuxSyntheticSessionErrorKind::guest_fault,
        .backend_error = {},
        .hle_error = {},
        .guest_fault = fault,
    };
}

}  // namespace

LinuxSyntheticSessionRunResult
run_linux_synthetic_session(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const HleRegistry& registry,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext initial_context) {
    GuestMemoryAccess guest_memory{
        image,
        prepared_memory};
    SyntheticHleTranscript transcript;
    GuestCpuContext context = initial_context;
    std::uint64_t gate_stop_count = 0;

    for (;;) {
        auto stopped =
            enter_linux_guest(
                image,
                prepared_memory,
                gate_region,
                context);
        if (!stopped.has_value()) {
            return LinuxSyntheticSessionRunResult::failure(
                backend_session_error(
                    stopped.error()));
        }

        if (stopped->reason ==
            ExecutionStopReason::guest_fault) {
            return LinuxSyntheticSessionRunResult::failure(
                fault_session_error(
                    stopped->fault));
        }

        if (stopped->reason !=
                ExecutionStopReason::host_gate ||
            !stopped->has_gate_slot) {
            return LinuxSyntheticSessionRunResult::failure(
                LinuxSyntheticSessionError{
                    .kind =
                        LinuxSyntheticSessionErrorKind::
                            unexpected_stop,
                    .backend_error = {},
                    .hle_error = {},
                    .guest_fault = {},
                });
        }

        ++gate_stop_count;

        auto handler =
            dispatch_synthetic_hle(
                registry,
                gate_region,
                stopped.value(),
                guest_memory,
                transcript);
        if (!handler.has_value()) {
            return LinuxSyntheticSessionRunResult::failure(
                hle_session_error(
                    handler.error()));
        }

        if (handler->action ==
            HleHandlerAction::exit) {
            return LinuxSyntheticSessionRunResult::success(
                LinuxSyntheticSessionResult{
                    .exit_code = handler->value,
                    .gate_stop_count = gate_stop_count,
                    .final_context = stopped->context,
                    .output =
                        std::move(
                            transcript.output),
                });
        }

        auto resumed =
            apply_synthetic_hle_resume(
                stopped->context,
                handler.value(),
                guest_memory);
        if (!resumed.has_value()) {
            return LinuxSyntheticSessionRunResult::failure(
                hle_session_error(
                    resumed.error()));
        }

        context = resumed.value();
    }
}

}  // namespace astraea::execution
