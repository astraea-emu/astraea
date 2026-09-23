#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/sce_agc_create_shader.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

enum class SceAgcShaderPreparationProfile {
    v18_pixel_public_shape,
    v18_geometry_es_public_shape,
};

enum class SceAgcShaderPatchKind {
    user_data_pointer,
    code_pointer,
    context_register_pointer,
    shader_register_pointer,
    specials_pointer,
    input_semantics_pointer,
    output_semantics_pointer,
    user_data_direct_resource_pointer,
    user_data_sharp_resource_pointer_0,
    user_data_sharp_resource_pointer_1,
    user_data_sharp_resource_pointer_2,
    user_data_sharp_resource_pointer_3,
    pixel_pgm_lo_value,
    pixel_pgm_hi_value,
    geometry_es_pgm_lo_value,
    geometry_es_pgm_hi_value,
    output_handle,
};

struct SceAgcShaderPatch {
    SceAgcShaderPatchKind kind =
        SceAgcShaderPatchKind::code_pointer;
    astraea::memory::GuestAddress address;
    std::vector<std::byte> bytes;

    auto operator<=>(const SceAgcShaderPatch&) const = default;
};

struct SceAgcShaderPreparationPlan {
    SceAgcCreateShaderPlan create_shader;
    SceAgcShaderPreparationProfile profile =
        SceAgcShaderPreparationProfile::
            v18_pixel_public_shape;
    astraea::memory::GuestAddress shader_handle;
    std::vector<SceAgcShaderPatch> patches;

    auto operator<=>(const SceAgcShaderPreparationPlan&) const = default;
};

enum class SceAgcShaderPreparationErrorCode {
    null_output_pointer,
    unsupported_header_version,
    unsupported_shader_stage,
    missing_register_list_provenance,
    canonical_register_list_mismatch,
    unsupported_pixel_program_register_pair,
    unsupported_geometry_program_register_pair,
    shader_code_address_misaligned,
    shader_code_address_unrepresentable,
    self_relative_pointer_overflow,
    self_relative_pointer_out_of_bounds,
    user_data_too_small,
    guest_address_overflow,
    output_aliases_shader_input,
    patch_overlap,
    host_allocation_failure,
};

struct SceAgcShaderPreparationError {
    SceAgcShaderPreparationErrorCode code =
        SceAgcShaderPreparationErrorCode::
            unsupported_header_version;
    std::optional<std::uint64_t> header_byte_offset;
    std::optional<astraea::memory::GuestAddress>
        guest_address;
    std::optional<SceAgcShaderPatchKind> patch_kind;

    auto operator<=>(
        const SceAgcShaderPreparationError&) const = default;
};

using SceAgcShaderPreparationResult =
    astraea::core::Result<
        SceAgcShaderPreparationPlan,
        SceAgcShaderPreparationError>;

[[nodiscard]] SceAgcShaderPreparationResult
plan_sce_agc_shader_preparation(
    const SceAgcCreateShaderPlan& create_shader);

enum class SceAgcShaderApplyErrorCode {
    guest_memory_preflight_failure,
    guest_memory_write_failure,
};

struct SceAgcShaderApplyError {
    SceAgcShaderApplyErrorCode code =
        SceAgcShaderApplyErrorCode::
            guest_memory_preflight_failure;
    std::size_t patch_index = 0;
    std::size_t patch_count = 0;
    std::size_t applied_count = 0;
    std::optional<GuestMemoryError> guest_memory_error;

    auto operator<=>(const SceAgcShaderApplyError&) const = default;
};

struct SceAgcShaderApplyReport {
    astraea::memory::GuestAddress shader_handle;
    std::size_t applied_patch_count = 0;

    auto operator<=>(const SceAgcShaderApplyReport&) const = default;
};

using SceAgcShaderApplyResult =
    astraea::core::Result<
        SceAgcShaderApplyReport,
        SceAgcShaderApplyError>;

// Applies only a previously validated preparation plan. Every patch range is
// checked for writability before the first mutation. The planner guarantees
// that the output-handle publication patch is last.
[[nodiscard]] SceAgcShaderApplyResult
apply_sce_agc_shader_preparation(
    const SceAgcShaderPreparationPlan& plan,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
