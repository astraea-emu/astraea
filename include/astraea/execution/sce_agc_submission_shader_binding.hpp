#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/sce_agc_driver_submit_dcb.hpp>
#include <astraea/execution/sce_agc_shader_registry.hpp>
#include <astraea/graphics/pm4_set_sh_reg_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>
#include <astraea/graphics/shader_register_state.hpp>

namespace astraea::execution {

struct SubmittedPixelProgramRegisterSource {
    std::size_t frame_index = 0;
    std::size_t packet_word_offset = 0;
    std::size_t value_word_offset = 0;

    auto operator<=>(const SubmittedPixelProgramRegisterSource&) const =
        default;
};

struct SceAgcSubmittedPixelShaderBinding {
    astraea::graphics::ShaderRegisterState shader_register_state;
    astraea::graphics::PixelProgramGpuAddress program_address;

    // Non-owning. Valid only until the next mutation of the source registry,
    // matching CreatedAgcShaderRegistry::lookup_unique().
    const CreatedAgcShader* shader = nullptr;

    SubmittedPixelProgramRegisterSource pgm_lo_source;
    SubmittedPixelProgramRegisterSource pgm_hi_source;
};

enum class SceAgcSubmittedPixelShaderBindingErrorCode {
    unsupported_submit_flag,
    pm4_framing_failure,
    unsupported_type3_header_control_bits,
    pm4_lowering_failure,
    shader_register_apply_failure,
    pixel_program_address_failure,
    missing_program_register_provenance,
    created_shader_lookup_failure,
};

struct SceAgcSubmittedPixelShaderBindingError {
    SceAgcSubmittedPixelShaderBindingErrorCode code =
        SceAgcSubmittedPixelShaderBindingErrorCode::
            unsupported_submit_flag;

    std::uint8_t submit_flag = 0;
    std::optional<std::size_t> frame_index;
    std::optional<std::size_t> word_offset;
    std::uint8_t type3_header_control_bits = 0;

    std::optional<astraea::graphics::Pm4Type3FrameError>
        pm4_framing_error;
    std::optional<astraea::graphics::Pm4SetShRegLowerError>
        pm4_lowering_error;
    std::optional<astraea::graphics::ShaderRegisterApplyError>
        shader_register_apply_error;
    std::optional<astraea::graphics::PixelProgramAddressError>
        pixel_program_address_error;
    std::optional<CreatedAgcShaderLookupError>
        created_shader_lookup_error;
};

using SceAgcSubmittedPixelShaderBindingResult =
    astraea::core::Result<
        SceAgcSubmittedPixelShaderBinding,
        SceAgcSubmittedPixelShaderBindingError>;

// Pure bounded V3 frontend proof.
//
// The first supported profile requires:
// - an already captured sceAgcDriverSubmitDcb submission with flag == 0;
// - generic PM4 Type-3 framing;
// - zero Type-3 low control bits;
// - only packet semantics that the existing shader-register Graphics IR can
//   apply;
// - both pixel PGM_LO and PGM_HI to be written by this submission.
//
// It never mutates guest memory or the registry, returns guest-visible
// SubmitDcb success, interprets draw/dispatch/resource packets, or calls
// Vulkan.
[[nodiscard]] SceAgcSubmittedPixelShaderBindingResult
plan_sce_agc_submitted_pixel_shader_binding(
    const SceAgcDcbSubmission& submission,
    const CreatedAgcShaderRegistry& registry);

}  // namespace astraea::execution
