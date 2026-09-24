#include <astraea/execution/sce_agc_first_raster_submission.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace astraea::execution {
namespace {

[[nodiscard]] SceAgcFirstRasterSubmissionError make_error(
    SceAgcFirstRasterSubmissionErrorCode code) noexcept {
    auto result = SceAgcFirstRasterSubmissionError{};
    result.code = code;
    return result;
}

void record_shader_register_source(
    const astraea::graphics::GraphicsIrEmission& emission,
    std::size_t frame_index,
    std::uint16_t target_register,
    std::optional<SceAgcRasterPacketSource>& output) noexcept {
    const auto* write =
        std::get_if<
            astraea::graphics::GraphicsIrShaderRegisterWriteRange>(
            &emission.operation);
    if (write == nullptr) {
        return;
    }

    const auto start =
        static_cast<std::size_t>(write->start_offset);
    const auto target =
        static_cast<std::size_t>(target_register);
    if (target < start ||
        target - start >= write->values.size()) {
        return;
    }

    const auto value_index = target - start;
    const auto packet_word_offset =
        emission.provenance.source_packet.extent.word_offset;

    output =
        SceAgcRasterPacketSource{
            .frame_index = frame_index,
            .packet_word_offset = packet_word_offset,
            .value_word_offset =
                packet_word_offset + 2U + value_index,
        };
}

void record_context_register_source(
    const astraea::graphics::GraphicsIrEmission& emission,
    std::size_t frame_index,
    std::uint16_t target_register,
    std::optional<SceAgcRasterPacketSource>& output) noexcept {
    const auto* write =
        std::get_if<
            astraea::graphics::GraphicsIrContextRegisterWriteRange>(
            &emission.operation);
    if (write == nullptr) {
        return;
    }

    const auto start =
        static_cast<std::size_t>(write->start_offset);
    const auto target =
        static_cast<std::size_t>(target_register);
    if (target < start ||
        target - start >= write->values.size()) {
        return;
    }

    const auto value_index = target - start;
    const auto packet_word_offset =
        emission.provenance.source_packet.extent.word_offset;

    output =
        SceAgcRasterPacketSource{
            .frame_index = frame_index,
            .packet_word_offset = packet_word_offset,
            .value_word_offset =
                packet_word_offset + 2U + value_index,
        };
}

void record_uconfig_register_source(
    const astraea::graphics::GraphicsIrEmission& emission,
    std::size_t frame_index,
    std::uint16_t target_register,
    std::optional<SceAgcRasterPacketSource>& output) noexcept {
    const auto* write =
        std::get_if<
            astraea::graphics::GraphicsIrUserConfigRegisterWriteRange>(
            &emission.operation);
    if (write == nullptr) {
        return;
    }

    const auto start =
        static_cast<std::size_t>(write->start_offset);
    const auto target =
        static_cast<std::size_t>(target_register);
    if (target < start ||
        target - start >= write->values.size()) {
        return;
    }

    const auto value_index = target - start;
    const auto packet_word_offset =
        emission.provenance.source_packet.extent.word_offset;

    output =
        SceAgcRasterPacketSource{
            .frame_index = frame_index,
            .packet_word_offset = packet_word_offset,
            .value_word_offset =
                packet_word_offset + 2U + value_index,
        };
}

struct ColorTargetSourceSnapshot {
    std::optional<SceAgcRasterPacketSource> target_mask;
    std::optional<SceAgcRasterPacketSource> base;
    std::optional<SceAgcRasterPacketSource> info;
    std::optional<SceAgcRasterPacketSource> base_ext;
    std::optional<SceAgcRasterPacketSource> attrib2;
    std::optional<SceAgcRasterPacketSource> attrib3;
};

struct DrawSnapshot {
    astraea::graphics::ShaderRegisterState shader_register_state;
    astraea::graphics::ContextRegisterState context_register_state;
    astraea::graphics::UserConfigRegisterState user_config_register_state;
    astraea::graphics::GraphicsIrSetInstanceCount instances;
    astraea::graphics::GraphicsIrDrawIndexAuto draw;

    std::optional<SceAgcRasterPacketSource> geometry_pgm_lo_source;
    std::optional<SceAgcRasterPacketSource> geometry_pgm_hi_source;
    std::optional<SceAgcRasterPacketSource> pixel_pgm_lo_source;
    std::optional<SceAgcRasterPacketSource> pixel_pgm_hi_source;
    std::optional<SceAgcRasterPacketSource> primitive_type_source;
    SceAgcRasterPacketSource instances_source;
    SceAgcRasterPacketSource draw_source;
    ColorTargetSourceSnapshot color_target_sources;
};

[[nodiscard]] bool complete_color_target_sources(
    const ColorTargetSourceSnapshot& sources) noexcept {
    return
        sources.target_mask.has_value() &&
        sources.base.has_value() &&
        sources.info.has_value() &&
        sources.base_ext.has_value() &&
        sources.attrib2.has_value() &&
        sources.attrib3.has_value();
}

}  // namespace

SceAgcFirstRasterSubmissionResult
plan_sce_agc_first_raster_submission(
    const SceAgcDcbSubmission& submission) {
    if (submission.flag != 0U) {
        auto error =
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    unsupported_submit_flag);
        error.submit_flag = submission.flag;
        return SceAgcFirstRasterSubmissionResult::failure(
            std::move(error));
    }

    auto framed =
        astraea::graphics::frame_pm4_type3_stream(
            submission.command_buffer_bytes);
    if (!framed.has_value()) {
        auto error =
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    pm4_framing_failure);
        error.word_offset =
            framed.error().word_offset;
        error.pm4_framing_error =
            framed.error();
        return SceAgcFirstRasterSubmissionResult::failure(
            std::move(error));
    }

    astraea::graphics::ShaderRegisterState shader_state{};
    astraea::graphics::ContextRegisterState context_state{};
    astraea::graphics::UserConfigRegisterState user_config_state{};

    std::optional<
        astraea::graphics::GraphicsIrSetInstanceCount>
        current_instances;
    std::optional<SceAgcRasterPacketSource>
        current_instances_source;

    std::optional<SceAgcRasterPacketSource>
        geometry_pgm_lo_source;
    std::optional<SceAgcRasterPacketSource>
        geometry_pgm_hi_source;
    std::optional<SceAgcRasterPacketSource>
        pixel_pgm_lo_source;
    std::optional<SceAgcRasterPacketSource>
        pixel_pgm_hi_source;
    std::optional<SceAgcRasterPacketSource>
        primitive_type_source;

    ColorTargetSourceSnapshot color_target_sources{};
    std::optional<DrawSnapshot> draw_snapshot;

    for (std::size_t frame_index = 0U;
         frame_index < framed->frames.size();
         ++frame_index) {
        const auto& frame =
            framed->frames[frame_index];

        if (frame.header.low_control_bits != 0U) {
            auto error =
                make_error(
                    SceAgcFirstRasterSubmissionErrorCode::
                        unsupported_type3_header_control_bits);
            error.frame_index = frame_index;
            error.word_offset = frame.word_offset;
            error.opcode = frame.header.opcode;
            error.type3_header_control_bits =
                frame.header.low_control_bits;
            return SceAgcFirstRasterSubmissionResult::failure(
                std::move(error));
        }

        switch (frame.header.opcode) {
        case astraea::graphics::kPm4SetShRegOpcode: {
            auto lowered =
                astraea::graphics::
                    lower_pm4_set_sh_reg_frame_to_graphics_ir(
                        frame);
            if (!lowered.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            set_sh_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.set_sh_lowering_error =
                    lowered.error();
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            const auto& emission =
                lowered.value();
            auto applied =
                astraea::graphics::
                    apply_shader_register_graphics_ir(
                        emission,
                        shader_state);
            if (!applied.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            shader_register_apply_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.shader_register_apply_error =
                    applied.error();
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            record_shader_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kGeometryEsProgramLoRegisterOffset,
                geometry_pgm_lo_source);
            record_shader_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kGeometryEsProgramHiRegisterOffset,
                geometry_pgm_hi_source);
            record_shader_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kPixelProgramLoRegisterOffset,
                pixel_pgm_lo_source);
            record_shader_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kPixelProgramHiRegisterOffset,
                pixel_pgm_hi_source);
            break;
        }

        case astraea::graphics::kPm4SetContextRegOpcode: {
            auto lowered =
                astraea::graphics::
                    lower_pm4_set_context_reg_frame_to_graphics_ir(
                        frame);
            if (!lowered.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            set_context_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.set_context_lowering_error =
                    lowered.error();
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            const auto& emission =
                lowered.value();
            auto applied =
                astraea::graphics::
                    apply_context_register_graphics_ir(
                        emission,
                        context_state);
            if (!applied.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            context_register_apply_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.context_register_apply_error =
                    applied.error();
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            record_context_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kColorTargetMaskContextOffset,
                color_target_sources.target_mask);
            record_context_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kColorTarget0BaseContextOffset,
                color_target_sources.base);
            record_context_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kColorTarget0InfoContextOffset,
                color_target_sources.info);
            record_context_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kColorTarget0BaseExtContextOffset,
                color_target_sources.base_ext);
            record_context_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kColorTarget0Attrib2ContextOffset,
                color_target_sources.attrib2);
            record_context_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kColorTarget0Attrib3ContextOffset,
                color_target_sources.attrib3);
            break;
        }

        case astraea::graphics::kPm4SetUconfigRegOpcode: {
            auto lowered =
                astraea::graphics::
                    lower_pm4_set_uconfig_reg_frame_to_graphics_ir(
                        frame);
            if (!lowered.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            set_uconfig_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.set_uconfig_lowering_error =
                    lowered.error();
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            const auto& emission =
                lowered.value();
            auto applied =
                astraea::graphics::
                    apply_user_config_register_graphics_ir(
                        emission,
                        user_config_state);
            if (!applied.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            user_config_register_apply_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.user_config_register_apply_error =
                    applied.error();
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            record_uconfig_register_source(
                emission,
                frame_index,
                astraea::graphics::
                    kFirstRasterPrimitiveTypeUconfigOffset,
                primitive_type_source);
            break;
        }

        case astraea::graphics::kPm4NumInstancesOpcode:
        case astraea::graphics::kPm4DrawIndexAutoOpcode: {
            auto lowered =
                astraea::graphics::
                    lower_pm4_draw_control_frame_to_graphics_ir(
                        frame);
            if (!lowered.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            draw_control_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.draw_control_lowering_error =
                    lowered.error();
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            const auto& emission =
                lowered.value();
            if (const auto* instances =
                    std::get_if<
                        astraea::graphics::
                            GraphicsIrSetInstanceCount>(
                        &emission.operation);
                instances != nullptr) {
                current_instances = *instances;
                current_instances_source =
                    SceAgcRasterPacketSource{
                        .frame_index = frame_index,
                        .packet_word_offset =
                            frame.word_offset,
                        .value_word_offset =
                            frame.word_offset + 1U,
                    };
                break;
            }

            const auto* draw =
                std::get_if<
                    astraea::graphics::
                        GraphicsIrDrawIndexAuto>(
                    &emission.operation);
            if (draw == nullptr) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            unsupported_opcode);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            if (draw_snapshot.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            multiple_draws);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            if (!current_instances.has_value() ||
                !current_instances_source.has_value()) {
                auto error =
                    make_error(
                        SceAgcFirstRasterSubmissionErrorCode::
                            missing_instance_count_before_draw);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                return SceAgcFirstRasterSubmissionResult::failure(
                    std::move(error));
            }

            draw_snapshot =
                DrawSnapshot{
                    .shader_register_state =
                        shader_state,
                    .context_register_state =
                        context_state,
                    .user_config_register_state =
                        user_config_state,
                    .instances =
                        current_instances.value(),
                    .draw = *draw,
                    .geometry_pgm_lo_source =
                        geometry_pgm_lo_source,
                    .geometry_pgm_hi_source =
                        geometry_pgm_hi_source,
                    .pixel_pgm_lo_source =
                        pixel_pgm_lo_source,
                    .pixel_pgm_hi_source =
                        pixel_pgm_hi_source,
                    .primitive_type_source =
                        primitive_type_source,
                    .instances_source =
                        current_instances_source.value(),
                    .draw_source =
                        SceAgcRasterPacketSource{
                            .frame_index = frame_index,
                            .packet_word_offset =
                                frame.word_offset,
                            .value_word_offset =
                                frame.word_offset + 1U,
                        },
                    .color_target_sources =
                        color_target_sources,
                };
            break;
        }

        default: {
            auto error =
                make_error(
                    SceAgcFirstRasterSubmissionErrorCode::
                        unsupported_opcode);
            error.frame_index = frame_index;
            error.word_offset = frame.word_offset;
            error.opcode = frame.header.opcode;
            return SceAgcFirstRasterSubmissionResult::failure(
                std::move(error));
        }
        }
    }

    if (!draw_snapshot.has_value()) {
        return SceAgcFirstRasterSubmissionResult::failure(
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    missing_draw));
    }

    auto snapshot =
        std::move(draw_snapshot).value();

    auto geometry_address =
        astraea::graphics::
            resolve_geometry_es_program_address(
                snapshot.shader_register_state);
    if (!geometry_address.has_value()) {
        auto error =
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    geometry_program_address_failure);
        error.geometry_program_address_error =
            geometry_address.error();
        return SceAgcFirstRasterSubmissionResult::failure(
            std::move(error));
    }

    auto pixel_address =
        astraea::graphics::
            resolve_pixel_program_address(
                snapshot.shader_register_state);
    if (!pixel_address.has_value()) {
        auto error =
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    pixel_program_address_failure);
        error.pixel_program_address_error =
            pixel_address.error();
        return SceAgcFirstRasterSubmissionResult::failure(
            std::move(error));
    }

    if (!snapshot.geometry_pgm_lo_source.has_value() ||
        !snapshot.geometry_pgm_hi_source.has_value() ||
        !snapshot.pixel_pgm_lo_source.has_value() ||
        !snapshot.pixel_pgm_hi_source.has_value()) {
        return SceAgcFirstRasterSubmissionResult::failure(
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    missing_program_register_provenance));
    }

    auto draw_plan =
        astraea::graphics::
            plan_first_raster_draw(
                snapshot.user_config_register_state,
                snapshot.instances,
                snapshot.draw);
    if (!draw_plan.has_value()) {
        auto error =
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    draw_plan_failure);
        error.draw_plan_error =
            draw_plan.error();
        return SceAgcFirstRasterSubmissionResult::failure(
            std::move(error));
    }

    auto color_target =
        astraea::graphics::
            resolve_color_target0_context_state(
                snapshot.context_register_state);
    if (!color_target.has_value()) {
        auto error =
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    color_target_decode_failure);
        error.color_target_error =
            color_target.error();
        return SceAgcFirstRasterSubmissionResult::failure(
            std::move(error));
    }

    auto color_target_image =
        astraea::graphics::
            plan_gfx10_linear_rgba8_unorm_color_target_image(
                color_target.value());
    if (!color_target_image.has_value()) {
        auto error =
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    color_target_image_failure);
        error.color_target_image_error =
            color_target_image.error();
        return SceAgcFirstRasterSubmissionResult::failure(
            std::move(error));
    }

    if (!snapshot.primitive_type_source.has_value()) {
        return SceAgcFirstRasterSubmissionResult::failure(
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    missing_primitive_provenance));
    }

    if (!complete_color_target_sources(
            snapshot.color_target_sources)) {
        return SceAgcFirstRasterSubmissionResult::failure(
            make_error(
                SceAgcFirstRasterSubmissionErrorCode::
                    missing_color_target_provenance));
    }

    return SceAgcFirstRasterSubmissionResult::success(
        SceAgcFirstRasterSubmissionPlan{
            .shader_register_state =
                std::move(snapshot.shader_register_state),
            .context_register_state =
                std::move(snapshot.context_register_state),
            .user_config_register_state =
                std::move(snapshot.user_config_register_state),
            .geometry_program_address =
                geometry_address.value(),
            .pixel_program_address =
                pixel_address.value(),
            .color_target_state =
                color_target.value(),
            .color_target_image =
                color_target_image.value(),
            .draw = draw_plan.value(),
            .geometry_pgm_lo_source =
                snapshot.geometry_pgm_lo_source.value(),
            .geometry_pgm_hi_source =
                snapshot.geometry_pgm_hi_source.value(),
            .pixel_pgm_lo_source =
                snapshot.pixel_pgm_lo_source.value(),
            .pixel_pgm_hi_source =
                snapshot.pixel_pgm_hi_source.value(),
            .primitive_type_source =
                snapshot.primitive_type_source.value(),
            .instances_source =
                snapshot.instances_source,
            .draw_source =
                snapshot.draw_source,
            .color_target_sources =
                SceAgcColorTargetPacketSources{
                    .target_mask =
                        snapshot.color_target_sources.
                            target_mask.value(),
                    .base =
                        snapshot.color_target_sources.
                            base.value(),
                    .info =
                        snapshot.color_target_sources.
                            info.value(),
                    .base_ext =
                        snapshot.color_target_sources.
                            base_ext.value(),
                    .attrib2 =
                        snapshot.color_target_sources.
                            attrib2.value(),
                    .attrib3 =
                        snapshot.color_target_sources.
                            attrib3.value(),
                },
        });
}

}  // namespace astraea::execution
