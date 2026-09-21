#include <astraea/execution/hle_runtime.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace astraea::execution {
namespace {

[[nodiscard]] HleRuntimeError runtime_error(
    HleRuntimeErrorCode code,
    bool has_function_id = false,
    HleFunctionId function_id = {},
    bool has_gate_slot = false,
    std::uint32_t gate_slot = 0,
    bool has_guest_address = false,
    std::uint64_t guest_address = 0) noexcept {
    return HleRuntimeError{
        .code = code,
        .has_function_id = has_function_id,
        .function_id = function_id,
        .has_gate_slot = has_gate_slot,
        .gate_slot = gate_slot,
        .has_guest_address = has_guest_address,
        .guest_address = guest_address,
        .has_guest_memory_error = false,
        .guest_memory_error = {},
    };
}

[[nodiscard]] HleRuntimeError memory_runtime_error(
    HleFunctionId function_id,
    std::uint32_t gate_slot,
    GuestMemoryError memory_error) noexcept {
    return HleRuntimeError{
        .code = HleRuntimeErrorCode::guest_memory_failure,
        .has_function_id = true,
        .function_id = function_id,
        .has_gate_slot = true,
        .gate_slot = gate_slot,
        .has_guest_address =
            memory_error.has_guest_address,
        .guest_address =
            memory_error.guest_address,
        .has_guest_memory_error = true,
        .guest_memory_error = memory_error,
    };
}

[[nodiscard]] HleCall make_call(
    HleFunctionId function_id,
    std::uint32_t gate_slot,
    const GuestCpuContext& context) noexcept {
    return HleCall{
        .function_id = function_id,
        .gate_slot = gate_slot,
        .guest_rip = context.rip,
        .guest_rsp = context.rsp,
        .arguments =
            {
                context.rdi,
                context.rsi,
                context.rdx,
                context.rcx,
                context.r8,
                context.r9,
            },
    };
}

}  // namespace

HleDispatchResult dispatch_synthetic_hle(
    const HleRegistry& registry,
    const SyntheticGateRegion& gate_region,
    const ExecutionStop& stop,
    const GuestMemoryAccess& guest_memory,
    SyntheticHleTranscript& transcript) {
    if (stop.reason != ExecutionStopReason::host_gate ||
        !stop.has_gate_slot) {
        return HleDispatchResult::failure(
            runtime_error(
                HleRuntimeErrorCode::
                    invalid_execution_stop));
    }

    const auto* binding =
        gate_region.binding_for_slot(
            stop.gate_slot);
    if (binding == nullptr) {
        return HleDispatchResult::failure(
            runtime_error(
                HleRuntimeErrorCode::unbound_gate,
                false,
                {},
                true,
                stop.gate_slot));
    }

    const auto* descriptor =
        registry.find(binding->function_id);
    if (descriptor == nullptr) {
        return HleDispatchResult::failure(
            runtime_error(
                HleRuntimeErrorCode::unknown_function,
                true,
                binding->function_id,
                true,
                stop.gate_slot));
    }

    const auto call =
        make_call(
            binding->function_id,
            stop.gate_slot,
            stop.context);

    if (descriptor->id == kSyntheticTestWriteId) {
        const std::uint64_t raw_count =
            call.arguments[1];
        if constexpr (
            sizeof(std::size_t) <
            sizeof(std::uint64_t)) {
            if (raw_count >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::
                        max())) {
                return HleDispatchResult::failure(
                    runtime_error(
                        HleRuntimeErrorCode::
                            host_size_unrepresentable,
                        true,
                        descriptor->id,
                        true,
                        stop.gate_slot,
                        true,
                        call.arguments[0]));
            }
        }

        const auto count =
            static_cast<std::size_t>(raw_count);
        const auto old_size =
            transcript.output.size();

        if (count >
            transcript.output.max_size() -
                old_size) {
            return HleDispatchResult::failure(
                runtime_error(
                    HleRuntimeErrorCode::
                        host_size_unrepresentable,
                    true,
                    descriptor->id,
                    true,
                    stop.gate_slot,
                    true,
                    call.arguments[0]));
        }

        if (count != 0) {
            try {
                transcript.output.resize(
                    old_size + count);
            } catch (const std::bad_alloc&) {
                return HleDispatchResult::failure(
                    runtime_error(
                        HleRuntimeErrorCode::
                            host_allocation_failure,
                        true,
                        descriptor->id,
                        true,
                        stop.gate_slot,
                        true,
                        call.arguments[0]));
            } catch (const std::length_error&) {
                return HleDispatchResult::failure(
                    runtime_error(
                        HleRuntimeErrorCode::
                            host_size_unrepresentable,
                        true,
                        descriptor->id,
                        true,
                        stop.gate_slot,
                        true,
                        call.arguments[0]));
            }

            auto copied =
                guest_memory.read(
                    astraea::memory::GuestAddress{
                        call.arguments[0]},
                    std::span<std::byte>{
                        transcript.output.data() +
                            old_size,
                        count});
            if (!copied.has_value()) {
                transcript.output.resize(old_size);
                return HleDispatchResult::failure(
                    memory_runtime_error(
                        descriptor->id,
                        stop.gate_slot,
                        copied.error()));
            }
        }

        return HleDispatchResult::success(
            HleHandlerResult{
                .action =
                    HleHandlerAction::resume,
                .value = raw_count,
            });
    }

    if (descriptor->id == kSyntheticTestExitId) {
        return HleDispatchResult::success(
            HleHandlerResult{
                .action =
                    HleHandlerAction::exit,
                .value = call.arguments[0],
            });
    }

    return HleDispatchResult::failure(
        runtime_error(
            HleRuntimeErrorCode::
                unsupported_synthetic_service,
            true,
            descriptor->id,
            true,
            stop.gate_slot));
}

HleResumeResult apply_synthetic_hle_resume(
    GuestCpuContext context,
    const HleHandlerResult& handler_result,
    const GuestMemoryAccess& guest_memory) {
    if (handler_result.action !=
        HleHandlerAction::resume) {
        return HleResumeResult::failure(
            runtime_error(
                HleRuntimeErrorCode::
                    invalid_execution_stop));
    }

    std::array<std::byte, 8> return_bytes{};
    auto copied =
        guest_memory.read(
            astraea::memory::GuestAddress{
                context.rsp},
            return_bytes);
    if (!copied.has_value()) {
        auto error =
            memory_runtime_error(
                {},
                0,
                copied.error());
        error.has_function_id = false;
        error.has_gate_slot = false;
        return HleResumeResult::failure(
            error);
    }

    std::uint64_t return_rip = 0;
    for (std::size_t i = 0;
         i < return_bytes.size();
         ++i) {
        return_rip |=
            static_cast<std::uint64_t>(
                std::to_integer<unsigned char>(
                    return_bytes[i]))
            << (i * 8U);
    }

    auto next_rsp =
        astraea::memory::GuestAddress::checked_add(
            astraea::memory::GuestAddress{
                context.rsp},
            astraea::memory::GuestSize{8});
    if (!next_rsp.has_value()) {
        return HleResumeResult::failure(
            runtime_error(
                HleRuntimeErrorCode::
                    guest_stack_pointer_overflow,
                false,
                {},
                false,
                0,
                true,
                context.rsp));
    }

    if (!guest_memory.is_exact_executable_address(
            astraea::memory::GuestAddress{
                return_rip})) {
        return HleResumeResult::failure(
            runtime_error(
                HleRuntimeErrorCode::
                    guest_return_address_not_executable,
                false,
                {},
                false,
                0,
                true,
                return_rip));
    }

    context.rax = handler_result.value;
    context.rip = return_rip;
    context.rsp = next_rsp->value();

    return HleResumeResult::success(
        context);
}

}  // namespace astraea::execution
