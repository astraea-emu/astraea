#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/sce_agc_driver_submit_dcb.hpp>
#include <astraea/graphics/color_target_state.hpp>
#include <astraea/graphics/context_register_state.hpp>
#include <astraea/graphics/first_raster_draw_plan.hpp>
#include <astraea/graphics/gfx10_color_target_image.hpp>
#include <astraea/graphics/pm4_draw_control_ir.hpp>
#include <astraea/graphics/pm4_set_context_reg_ir.hpp>
#include <astraea/graphics/pm4_set_sh_reg_ir.hpp>
#include <astraea/graphics/pm4_set_uconfig_reg_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>
#include <astraea/graphics/shader_register_state.hpp>
#include <astraea/graphics/user_config_register_state.hpp>

namespace astraea::execution {

struct SceAgcRasterPacketSource {
    std::size_t frame_index = 0;
    std::size_t packet_word_offset = 0;
    std::size_t value_word_offset = 0;

    auto operator<=>(const SceAgcRasterPacketSource&) const = default;
};

struct SceAgcColorTargetPacketSources {
    SceAgcRasterPacketSource target_mask;
    SceAgcRasterPacketSource base;
    SceAgcRasterPacketSource info;
    SceAgcRasterPacketSource base_ext;
    SceAgcRasterPacketSource attrib2;
    SceAgcRasterPacketSource attrib3;

    auto operator<=>(const SceAgcColorTargetPacketSources&) const =
        default;
};

struct SceAgcFirstRasterSubmissionPlan {
    // Immutable state snapshot at the accepted draw.
    astraea::graphics::ShaderRegisterState shader_register_state;
    astraea::graphics::ContextRegisterState context_register_state;
    astraea::graphics::UserConfigRegisterState user_config_register_state;

    astraea::graphics::GeometryEsProgramGpuAddress
        geometry_program_address;
    astraea::graphics::PixelProgramGpuAddress
        pixel_program_address;

    astraea::graphics::ColorTarget0ContextState color_target_state;
    astraea::graphics::GuestGpuImageDescriptor color_target_image;

    astraea::graphics::FirstRasterDrawPlan draw;

    SceAgcRasterPacketSource geometry_pgm_lo_source;
    SceAgcRasterPacketSource geometry_pgm_hi_source;
    SceAgcRasterPacketSource pixel_pgm_lo_source;
    SceAgcRasterPacketSource pixel_pgm_hi_source;
    SceAgcRasterPacketSource primitive_type_source;
    SceAgcRasterPacketSource instances_source;
    SceAgcRasterPacketSource draw_source;
    SceAgcColorTargetPacketSources color_target_sources;
};

enum class SceAgcFirstRasterSubmissionErrorCode {
    unsupported_submit_flag,
    pm4_framing_failure,
    unsupported_type3_header_control_bits,
    unsupported_opcode,
    set_sh_lowering_failure,
    shader_register_apply_failure,
    set_context_lowering_failure,
    context_register_apply_failure,
    set_uconfig_lowering_failure,
    user_config_register_apply_failure,
    draw_control_lowering_failure,
    missing_instance_count_before_draw,
    multiple_draws,
    missing_draw,
    geometry_program_address_failure,
    pixel_program_address_failure,
    missing_program_register_provenance,
    missing_primitive_provenance,
    missing_color_target_provenance,
    draw_plan_failure,
    color_target_decode_failure,
    color_target_image_failure,
};

struct SceAgcFirstRasterSubmissionError {
    SceAgcFirstRasterSubmissionErrorCode code =
        SceAgcFirstRasterSubmissionErrorCode::
            unsupported_submit_flag;

    std::uint8_t submit_flag = 0;
    std::optional<std::size_t> frame_index;
    std::optional<std::size_t> word_offset;
    std::uint8_t opcode = 0;
    std::uint8_t type3_header_control_bits = 0;

    std::optional<astraea::graphics::Pm4Type3FrameError>
        pm4_framing_error;
    std::optional<astraea::graphics::Pm4SetShRegLowerError>
        set_sh_lowering_error;
    std::optional<astraea::graphics::ShaderRegisterApplyError>
        shader_register_apply_error;
    std::optional<astraea::graphics::Pm4SetContextRegLowerError>
        set_context_lowering_error;
    std::optional<astraea::graphics::ContextRegisterApplyError>
        context_register_apply_error;
    std::optional<astraea::graphics::Pm4SetUconfigRegLowerError>
        set_uconfig_lowering_error;
    std::optional<astraea::graphics::UserConfigRegisterApplyError>
        user_config_register_apply_error;
    std::optional<astraea::graphics::Pm4DrawControlLowerError>
        draw_control_lowering_error;
    std::optional<astraea::graphics::GeometryEsProgramAddressError>
        geometry_program_address_error;
    std::optional<astraea::graphics::PixelProgramAddressError>
        pixel_program_address_error;
    std::optional<astraea::graphics::FirstRasterDrawPlanError>
        draw_plan_error;
    std::optional<astraea::graphics::ColorTarget0ContextError>
        color_target_error;
    std::optional<astraea::graphics::Gfx10ColorTargetImageError>
        color_target_image_error;
};

using SceAgcFirstRasterSubmissionResult =
    astraea::core::Result<
        SceAgcFirstRasterSubmissionPlan,
        SceAgcFirstRasterSubmissionError>;

// Reduces the bounded first owned raster DCB profile into one immutable
// draw-time frontend plan.
//
// Supported Type-3 domains:
// - SET_SH_REG;
// - SET_CONTEXT_REG;
// - SET_UCONFIG_REG;
// - NUM_INSTANCES;
// - DRAW_INDEX_AUTO.
//
// State is applied in packet order and snapshotted at the single accepted
// draw. The plan resolves only already-verified program-address and
// ColorTarget/image semantics. It performs no registry lookup, guest-memory
// mutation, shader compilation, Vulkan work, or guest-visible SubmitDcb
// success.
[[nodiscard]] SceAgcFirstRasterSubmissionResult
plan_sce_agc_first_raster_submission(
    const SceAgcDcbSubmission& submission);

}  // namespace astraea::execution
