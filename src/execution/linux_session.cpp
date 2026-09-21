#include <astraea/execution/linux_session.hpp>

#include <cstdint>
#include <new>
#include <stdexcept>
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

[[nodiscard]] LinuxSyntheticSessionError allocation_session_error() noexcept {
    return LinuxSyntheticSessionError{
        .kind =
            LinuxSyntheticSessionErrorKind::
                host_allocation_failure,
        .backend_error = {},
        .hle_error = {},
        .guest_fault = {},
    };
}

[[nodiscard]] bool append_event(
    std::vector<SyntheticSessionEvent>& events,
    SyntheticSessionEvent event) noexcept {
    try {
        events.push_back(std::move(event));
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
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
    std::vector<SyntheticSessionEvent> events;

    for (;;) {
        if (!append_event(
                events,
                SyntheticSessionEvent{
                    .kind =
                        SyntheticSessionEventKind::
                            guest_entry,
                    .rip = context.rip,
                    .rsp = context.rsp,
                    .has_gate_slot = false,
                    .gate_slot = 0,
                    .has_function_id = false,
                    .function_id = {},
                    .value = 0,
                })) {
            return LinuxSyntheticSessionRunResult::failure(
                allocation_session_error());
        }

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

        const auto* binding =
            gate_region.binding_for_slot(
                stopped->gate_slot);

        if (!append_event(
                events,
                SyntheticSessionEvent{
                    .kind =
                        SyntheticSessionEventKind::
                            gate_stop,
                    .rip = stopped->context.rip,
                    .rsp = stopped->context.rsp,
                    .has_gate_slot = true,
                    .gate_slot = stopped->gate_slot,
                    .has_function_id =
                        binding != nullptr,
                    .function_id =
                        binding != nullptr
                            ? binding->function_id
                            : HleFunctionId{},
                    .value = 0,
                })) {
            return LinuxSyntheticSessionRunResult::failure(
                allocation_session_error());
        }

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
            if (!append_event(
                    events,
                    SyntheticSessionEvent{
                        .kind =
                            SyntheticSessionEventKind::
                                hle_exit,
                        .rip = stopped->context.rip,
                        .rsp = stopped->context.rsp,
                        .has_gate_slot = true,
                        .gate_slot = stopped->gate_slot,
                        .has_function_id =
                            binding != nullptr,
                        .function_id =
                            binding != nullptr
                                ? binding->function_id
                                : HleFunctionId{},
                        .value = handler->value,
                    })) {
                return LinuxSyntheticSessionRunResult::failure(
                    allocation_session_error());
            }

            return LinuxSyntheticSessionRunResult::success(
                LinuxSyntheticSessionResult{
                    .exit_code = handler->value,
                    .gate_stop_count = gate_stop_count,
                    .final_context = stopped->context,
                    .output =
                        std::move(
                            transcript.output),
                    .events = std::move(events),
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

        if (!append_event(
                events,
                SyntheticSessionEvent{
                    .kind =
                        SyntheticSessionEventKind::
                            hle_resume,
                    .rip = resumed->rip,
                    .rsp = resumed->rsp,
                    .has_gate_slot = true,
                    .gate_slot = stopped->gate_slot,
                    .has_function_id =
                        binding != nullptr,
                    .function_id =
                        binding != nullptr
                            ? binding->function_id
                            : HleFunctionId{},
                    .value = handler->value,
                })) {
            return LinuxSyntheticSessionRunResult::failure(
                allocation_session_error());
        }

        context = resumed.value();
    }
}

}  // namespace astraea::execution
