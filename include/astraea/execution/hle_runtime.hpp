#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_agc_create_shader.hpp>
#include <astraea/execution/sce_agc_shader_preparation.hpp>
#include <astraea/execution/sce_agc_shader_registry.hpp>

namespace astraea::execution {

inline constexpr HleFunctionId kSyntheticTestWriteId{1};
inline constexpr HleFunctionId kSyntheticTestExitId{2};

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
    sce_agc_create_shader_plan_failure,
    sce_agc_shader_preparation_failure,
    sce_agc_shader_materialization_failure,
    sce_agc_shader_registration_failure,
    sce_agc_shader_registry_rollback_failure,
    sce_agc_shader_apply_failure,
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
    std::optional<SceAgcCreateShaderPlanError>
        sce_agc_create_shader_plan_error;
    std::optional<SceAgcShaderPreparationError>
        sce_agc_shader_preparation_error;
    std::optional<SceAgcShaderApplyError>
        sce_agc_shader_apply_error;
    std::optional<CreatedAgcShaderMaterializationError>
        sce_agc_shader_materialization_error;
    std::optional<CreatedAgcShaderRegistrationError>
        sce_agc_shader_registration_error;

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

struct HleDispatchState {
    // Test-only write-service output remains isolated from real service state.
    std::vector<std::byte> output;

    // Persistent host-side identity for successfully created AGC shaders.
    CreatedAgcShaderRegistry created_agc_shaders;
};

// Compatibility name retained for existing synthetic-service tests.
using SyntheticHleTranscript = HleDispatchState;

using HleDispatchResult =
    astraea::core::Result<HleHandlerResult, HleRuntimeError>;
using HleResumeResult =
    astraea::core::Result<GuestCpuContext, HleRuntimeError>;

[[nodiscard]] HleDispatchResult dispatch_hle(
    const HleRegistry& registry,
    const SyntheticGateRegion& gate_region,
    const ExecutionStop& stop,
    const GuestMemoryAccess& guest_memory,
    HleDispatchState& state);

// Compatibility wrapper for the pre-V1 synthetic test surface.
[[nodiscard]] HleDispatchResult dispatch_synthetic_hle(
    const HleRegistry& registry,
    const SyntheticGateRegion& gate_region,
    const ExecutionStop& stop,
    const GuestMemoryAccess& guest_memory,
    SyntheticHleTranscript& transcript);

[[nodiscard]] HleResumeResult apply_hle_resume(
    GuestCpuContext context,
    const HleHandlerResult& handler_result,
    const GuestMemoryAccess& guest_memory);

// Compatibility wrapper for existing synthetic-session tests.
[[nodiscard]] HleResumeResult apply_synthetic_hle_resume(
    GuestCpuContext context,
    const HleHandlerResult& handler_result,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
