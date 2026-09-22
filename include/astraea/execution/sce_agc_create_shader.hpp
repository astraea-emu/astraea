#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/graphics/agc_shader_binary.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

// Astraea-internal service identity. The public PS5 NID/library identity is
// resolved to this ID by the SCE import/HLE binding layer; it is not encoded
// into HleFunctionId itself.
inline constexpr HleFunctionId kSceAgcCreateShaderHleId{3};

struct SceAgcCreateShaderRequest {
    astraea::memory::GuestAddress output_pointer_address;
    astraea::memory::GuestAddress shader_header_address;
    astraea::memory::GuestAddress shader_text_address;

    auto operator<=>(const SceAgcCreateShaderRequest&) const = default;
};

struct SceAgcCreateShaderPlan {
    SceAgcCreateShaderRequest request;
    astraea::graphics::AgcShaderBinary shader;

    auto operator<=>(const SceAgcCreateShaderPlan&) const = default;
};

enum class SceAgcCreateShaderPlanErrorCode {
    unexpected_function,
    null_shader_header,
    null_shader_text,
    declared_header_size_too_small,
    declared_shader_text_size_too_small,
    host_size_unrepresentable,
    host_allocation_failure,
    guest_memory_failure,
    invalid_shader_binary,
};

struct SceAgcCreateShaderPlanError {
    SceAgcCreateShaderPlanErrorCode code =
        SceAgcCreateShaderPlanErrorCode::unexpected_function;
    std::optional<HleFunctionId> function_id;
    std::optional<astraea::memory::GuestAddress> guest_address;
    std::optional<GuestMemoryError> guest_memory_error;
    std::optional<astraea::graphics::AgcShaderBinaryError>
        shader_binary_error;

    auto operator<=>(const SceAgcCreateShaderPlanError&) const = default;
};

using SceAgcCreateShaderPlanResult =
    astraea::core::Result<
        SceAgcCreateShaderPlan,
        SceAgcCreateShaderPlanError>;

// Materializes only the immutable read/validate/plan half of
// sceAgcCreateShader. This function never mutates guest memory and does not
// claim that a native AGC shader object has been created.
[[nodiscard]] SceAgcCreateShaderPlanResult
plan_sce_agc_create_shader(
    const HleCall& call,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
