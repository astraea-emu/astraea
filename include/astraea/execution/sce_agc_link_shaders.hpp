#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_agc_shader_registry.hpp>
#include <astraea/graphics/agc_shader_binary.hpp>
#include <astraea/graphics/gpu_address.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

// Astraea-internal service identity. The public PS5 NID/library/module
// identity is resolved separately by the SCE import-binding layer.
inline constexpr HleFunctionId kSceAgcLinkShadersHleId{5};

inline constexpr std::size_t
    kSceAgcLinkShadersContextRecordCount = 34;
inline constexpr std::size_t
    kSceAgcLinkShadersUserConfigRecordCount = 3;
inline constexpr std::size_t
    kSceAgcLinkShadersRegisterRecordSize = 8;
inline constexpr std::size_t
    kSceAgcLinkShadersContextOutputSize =
        kSceAgcLinkShadersContextRecordCount *
        kSceAgcLinkShadersRegisterRecordSize;
inline constexpr std::size_t
    kSceAgcLinkShadersUserConfigOutputSize =
        kSceAgcLinkShadersUserConfigRecordCount *
        kSceAgcLinkShadersRegisterRecordSize;
inline constexpr std::uint32_t
    kSceAgcLinkShadersTriangleListPrimitiveType = 4U;

enum class SceAgcLinkShadersShaderSlot {
    pre_raster,
    pixel,
};

struct SceAgcLinkedShaderIdentity {
    astraea::memory::GuestAddress shader_handle;
    astraea::graphics::GpuVirtualAddress code_address;
    astraea::graphics::AgcShaderStage stage =
        astraea::graphics::AgcShaderStage::pixel;
    SceAgcShaderPreparationProfile preparation_profile =
        SceAgcShaderPreparationProfile::
            v18_pixel_public_shape;

    auto operator<=>(
        const SceAgcLinkedShaderIdentity&) const = default;
};

struct SceAgcLinkShadersPlan {
    astraea::memory::GuestAddress context_output_address;
    astraea::memory::GuestRange context_output_range;
    astraea::memory::GuestAddress user_config_output_address;
    astraea::memory::GuestRange user_config_output_range;
    astraea::memory::GuestAddress hull_shader_handle;
    SceAgcLinkedShaderIdentity pre_raster_shader;
    SceAgcLinkedShaderIdentity pixel_shader;
    std::uint32_t primitive_type = 0;

    auto operator<=>(const SceAgcLinkShadersPlan&) const = default;
};

enum class SceAgcLinkShadersPlanErrorCode {
    unexpected_function,
    null_context_output,
    null_user_config_output,
    unsupported_hull_shader,
    unsupported_primitive_type,
    output_range_overflow,
    output_ranges_overlap,
    same_shader_handle,
    pre_raster_handle_lookup_failure,
    pixel_handle_lookup_failure,
    unsupported_pre_raster_shader,
    unsupported_pixel_shader,
};

struct SceAgcLinkShadersPlanError {
    SceAgcLinkShadersPlanErrorCode code =
        SceAgcLinkShadersPlanErrorCode::
            unexpected_function;
    std::optional<HleFunctionId> function_id;
    std::optional<astraea::memory::GuestAddress>
        guest_address;
    std::optional<SceAgcLinkShadersShaderSlot>
        shader_slot;
    std::optional<CreatedAgcShaderHandleLookupError>
        handle_lookup_error;
    std::optional<astraea::graphics::AgcShaderStage>
        actual_stage;
    std::optional<SceAgcShaderPreparationProfile>
        actual_preparation_profile;
    std::uint32_t primitive_type = 0;

    auto operator<=>(
        const SceAgcLinkShadersPlanError&) const = default;
};

using SceAgcLinkShadersPlanResult =
    astraea::core::Result<
        SceAgcLinkShadersPlan,
        SceAgcLinkShadersPlanError>;

// Captures and validates only the immutable request/identity half of
// sceAgcLinkShaders. It performs no guest-memory access and no registry
// mutation. Output materialization is deliberately a later evidence gate.
[[nodiscard]] SceAgcLinkShadersPlanResult
plan_sce_agc_link_shaders(
    const HleCall& call,
    const CreatedAgcShaderRegistry& shader_registry) noexcept;

}  // namespace astraea::execution
