#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/hle.hpp>

namespace astraea::execution {

inline constexpr HleFunctionId kSyntheticTestWriteId{1};
inline constexpr HleFunctionId kSyntheticTestExitId{2};

struct HleCall {
    HleFunctionId function_id;
    std::uint32_t gate_slot = 0;
    std::uint64_t guest_rip = 0;
    std::uint64_t guest_rsp = 0;
    std::array<std::uint64_t, 6> arguments{};

    auto operator<=>(const HleCall&) const = default;
};

enum class HleRuntimeErrorCode {
    invalid_execution_stop,
    unbound_gate,
    unknown_function,
    unsupported_synthetic_service,
    host_size_unrepresentable,
    host_allocation_failure,
    guest_memory_failure,
    guest_stack_pointer_overflow,
    guest_return_address_not_executable,
};

struct HleRuntimeError {
    HleRuntimeErrorCode code =
        HleRuntimeErrorCode::invalid_execution_stop;
    bool has_function_id = false;
    HleFunctionId function_id;
    bool has_gate_slot = false;
    std::uint32_t gate_slot = 0;
    bool has_guest_address = false;
    std::uint64_t guest_address = 0;
    bool has_guest_memory_error = false;
    GuestMemoryError guest_memory_error;

    auto operator<=>(const HleRuntimeError&) const = default;
};

enum class HleHandlerAction {
    resume,
    exit,
};

struct HleHandlerResult {
    HleHandlerAction action = HleHandlerAction::resume;
    std::uint64_t value = 0;

    auto operator<=>(const HleHandlerResult&) const = default;
};

struct SyntheticHleTranscript {
    std::vector<std::byte> output;
};

using HleDispatchResult =
    astraea::core::Result<HleHandlerResult, HleRuntimeError>;
using HleResumeResult =
    astraea::core::Result<GuestCpuContext, HleRuntimeError>;

[[nodiscard]] HleDispatchResult dispatch_synthetic_hle(
    const HleRegistry& registry,
    const SyntheticGateRegion& gate_region,
    const ExecutionStop& stop,
    const GuestMemoryAccess& guest_memory,
    SyntheticHleTranscript& transcript);

[[nodiscard]] HleResumeResult apply_synthetic_hle_resume(
    GuestCpuContext context,
    const HleHandlerResult& handler_result,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
