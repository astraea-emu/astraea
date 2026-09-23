#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
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

inline constexpr std::size_t
    kSceAgcLinkShadersMeasuredInterpolantRecordCount = 32;
inline constexpr std::size_t
    kSceAgcLinkShadersMeasuredInterpolantByteCount =
        kSceAgcLinkShadersMeasuredInterpolantRecordCount *
        kSceAgcLinkShadersRegisterRecordSize;
inline constexpr std::uint64_t
    kSceAgcLinkShadersUnmeasuredContextByteOffset = 0x100U;
inline constexpr std::uint64_t
    kSceAgcLinkShadersMeasuredRoutingByteOffset = 0x108U;

struct SceAgcLinkShadersRegisterRecord {
    std::uint32_t offset = 0;
    std::uint32_t value = 0;

    auto operator<=>(
        const SceAgcLinkShadersRegisterRecord&) const = default;
};

enum class SceAgcLinkShadersOutputCompleteness {
    measured_partial,
};

enum class SceAgcLinkShadersMeasuredPatchKind {
    interpolant_table,
    gs_out_primitive_type,
};

struct SceAgcLinkShadersMeasuredOutputPlan {
    SceAgcLinkShadersOutputCompleteness completeness =
        SceAgcLinkShadersOutputCompleteness::measured_partial;
    astraea::memory::GuestAddress interpolant_output_address;
    astraea::memory::GuestRange interpolant_output_range;
    astraea::memory::GuestAddress routing_output_address;
    astraea::memory::GuestRange routing_output_range;
    astraea::memory::GuestAddress preserved_user_config_address;
    std::array<
        SceAgcLinkShadersRegisterRecord,
        kSceAgcLinkShadersMeasuredInterpolantRecordCount>
        interpolant_records{};
    SceAgcLinkShadersRegisterRecord routing_record;

    auto operator<=>(
        const SceAgcLinkShadersMeasuredOutputPlan&) const = default;
};

enum class SceAgcLinkShadersMeasuredOutputPlanErrorCode {
    measured_output_address_overflow,
};

struct SceAgcLinkShadersMeasuredOutputPlanError {
    SceAgcLinkShadersMeasuredOutputPlanErrorCode code =
        SceAgcLinkShadersMeasuredOutputPlanErrorCode::
            measured_output_address_overflow;
    std::optional<astraea::memory::GuestAddress>
        base_address;
    std::uint64_t byte_offset = 0;

    auto operator<=>(
        const SceAgcLinkShadersMeasuredOutputPlanError&) const = default;
};

using SceAgcLinkShadersMeasuredOutputPlanResult =
    astraea::core::Result<
        SceAgcLinkShadersMeasuredOutputPlan,
        SceAgcLinkShadersMeasuredOutputPlanError>;

// Materializes only the currently measured LinkShaders output subset. The
// context record at +0x100 and every user-config record remain intentionally
// absent from this plan.
[[nodiscard]] SceAgcLinkShadersMeasuredOutputPlanResult
materialize_sce_agc_link_shaders_measured_output(
    const SceAgcLinkShadersPlan& plan) noexcept;

enum class SceAgcLinkShadersMeasuredApplyErrorCode {
    guest_memory_preflight_failure,
    guest_memory_write_failure,
};

struct SceAgcLinkShadersMeasuredApplyError {
    SceAgcLinkShadersMeasuredApplyErrorCode code =
        SceAgcLinkShadersMeasuredApplyErrorCode::
            guest_memory_preflight_failure;
    SceAgcLinkShadersMeasuredPatchKind patch_kind =
        SceAgcLinkShadersMeasuredPatchKind::
            interpolant_table;
    std::size_t patch_index = 0;
    std::size_t patch_count = 2;
    std::size_t applied_patch_count = 0;
    std::size_t applied_record_count = 0;
    std::optional<GuestMemoryError> guest_memory_error;

    auto operator<=>(
        const SceAgcLinkShadersMeasuredApplyError&) const = default;
};

struct SceAgcLinkShadersMeasuredApplyReport {
    SceAgcLinkShadersOutputCompleteness completeness =
        SceAgcLinkShadersOutputCompleteness::measured_partial;
    std::size_t applied_patch_count = 0;
    std::size_t applied_context_record_count = 0;
    std::size_t applied_user_config_record_count = 0;

    auto operator<=>(
        const SceAgcLinkShadersMeasuredApplyReport&) const = default;
};

using SceAgcLinkShadersMeasuredApplyResult =
    astraea::core::Result<
        SceAgcLinkShadersMeasuredApplyReport,
        SceAgcLinkShadersMeasuredApplyError>;

// Preflights both measured context write ranges before the first mutation.
// The unmeasured context record at +0x100 and the entire user-config block are
// never touched by this function.
[[nodiscard]] SceAgcLinkShadersMeasuredApplyResult
apply_sce_agc_link_shaders_measured_output(
    const SceAgcLinkShadersMeasuredOutputPlan& plan,
    const GuestMemoryAccess& guest_memory) noexcept;

}  // namespace astraea::execution
