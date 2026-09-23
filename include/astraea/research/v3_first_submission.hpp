#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/sce_agc_driver_submit_dcb.hpp>
#include <astraea/execution/sce_agc_shader_registry.hpp>
#include <astraea/graphics/context_register_state.hpp>
#include <astraea/graphics/pm4_draw_control_ir.hpp>
#include <astraea/graphics/pm4_set_context_reg_ir.hpp>
#include <astraea/graphics/pm4_set_sh_reg_ir.hpp>
#include <astraea/graphics/pm4_set_uconfig_reg_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>
#include <astraea/graphics/shader_register_state.hpp>
#include <astraea/graphics/user_config_register_state.hpp>
#include <astraea/research/v3_first_draw_plan.hpp>

namespace astraea::research {

struct V3FirstSubmissionPacketSource {
    std::size_t frame_index = 0;
    std::size_t packet_word_offset = 0;
    std::size_t value_word_offset = 0;

    auto operator<=>(const V3FirstSubmissionPacketSource&) const =
        default;
};

struct V3FirstSubmissionPlan {
    // State snapshots at the instant of the one accepted draw.
    astraea::graphics::ShaderRegisterState shader_register_state;
    astraea::graphics::ContextRegisterState context_register_state;
    astraea::graphics::UserConfigRegisterState user_config_register_state;

    astraea::graphics::GeometryEsProgramGpuAddress geometry_program_address;
    astraea::graphics::PixelProgramGpuAddress pixel_program_address;

    // Non-owning references into the source registry. Valid only until its next
    // mutation.
    const astraea::execution::CreatedAgcShader* geometry_shader = nullptr;
    const astraea::execution::CreatedAgcShader* pixel_shader = nullptr;

    V3FirstDrawPlan draw;

    V3FirstSubmissionPacketSource geometry_pgm_lo_source;
    V3FirstSubmissionPacketSource geometry_pgm_hi_source;
    V3FirstSubmissionPacketSource pixel_pgm_lo_source;
    V3FirstSubmissionPacketSource pixel_pgm_hi_source;
    V3FirstSubmissionPacketSource instances_source;
    V3FirstSubmissionPacketSource draw_source;
};

enum class V3FirstSubmissionErrorCode {
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
    geometry_shader_lookup_failure,
    pixel_shader_lookup_failure,
    draw_plan_failure,
};

struct V3FirstSubmissionError {
    V3FirstSubmissionErrorCode code =
        V3FirstSubmissionErrorCode::
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
    std::optional<astraea::execution::CreatedAgcShaderStageLookupError>
        created_shader_lookup_error;
    std::optional<V3FirstDrawPlanError>
        draw_plan_error;
};

using V3FirstSubmissionResult =
    astraea::core::Result<
        V3FirstSubmissionPlan,
        V3FirstSubmissionError>;

// Reduces the exact first synthetic raster DCB profile into one immutable
// draw-time snapshot.
//
// Supported Type-3 packet domains:
// - SET_SH_REG;
// - SET_CONTEXT_REG;
// - SET_UCONFIG_REG;
// - NUM_INSTANCES;
// - DRAW_INDEX_AUTO.
//
// Packet order matters. Register state is snapshotted when the one draw packet
// is encountered; later writes cannot retroactively affect that draw.
// NUM_INSTANCES must have appeared before the draw. A second draw is rejected.
//
// The function performs no guest-memory writes, HLE dispatch, shader execution,
// resource resolution, or Vulkan work.
[[nodiscard]] V3FirstSubmissionResult
plan_v3_first_submission(
    const astraea::execution::SceAgcDcbSubmission& submission,
    const astraea::execution::CreatedAgcShaderRegistry& registry);

}  // namespace astraea::research
