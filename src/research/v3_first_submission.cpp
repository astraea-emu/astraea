#include <astraea/research/v3_first_submission.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace astraea::research {
namespace {

[[nodiscard]] V3FirstSubmissionError make_error(
    V3FirstSubmissionErrorCode code) noexcept {
    auto error = V3FirstSubmissionError{};
    error.code = code;
    return error;
}

void record_shader_register_source(
    const astraea::graphics::GraphicsIrEmission& emission,
    std::size_t frame_index,
    std::uint16_t target_register,
    std::optional<V3FirstSubmissionPacketSource>& output) noexcept {
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
        V3FirstSubmissionPacketSource{
            .frame_index = frame_index,
            .packet_word_offset = packet_word_offset,
            .value_word_offset =
                packet_word_offset + 2U + value_index,
        };
}

struct DrawSnapshot {
    astraea::graphics::ShaderRegisterState shader_register_state;
    astraea::graphics::ContextRegisterState context_register_state;
    astraea::graphics::UserConfigRegisterState user_config_register_state;
    astraea::graphics::GraphicsIrSetInstanceCount instances;
    astraea::graphics::GraphicsIrDrawIndexAuto draw;

    std::optional<V3FirstSubmissionPacketSource> geometry_pgm_lo_source;
    std::optional<V3FirstSubmissionPacketSource> geometry_pgm_hi_source;
    std::optional<V3FirstSubmissionPacketSource> pixel_pgm_lo_source;
    std::optional<V3FirstSubmissionPacketSource> pixel_pgm_hi_source;
    V3FirstSubmissionPacketSource instances_source;
    V3FirstSubmissionPacketSource draw_source;
};

}  // namespace

V3FirstSubmissionResult
plan_v3_first_submission(
    const astraea::execution::SceAgcDcbSubmission& submission,
    const astraea::execution::CreatedAgcShaderRegistry& registry) {
    if (submission.flag != 0U) {
        auto error =
            make_error(
                V3FirstSubmissionErrorCode::
                    unsupported_submit_flag);
        error.submit_flag = submission.flag;
        return V3FirstSubmissionResult::failure(
            std::move(error));
    }

    auto framed =
        astraea::graphics::frame_pm4_type3_stream(
            submission.command_buffer_bytes);
    if (!framed.has_value()) {
        auto error =
            make_error(
                V3FirstSubmissionErrorCode::
                    pm4_framing_failure);
        error.word_offset =
            framed.error().word_offset;
        error.pm4_framing_error =
            framed.error();
        return V3FirstSubmissionResult::failure(
            std::move(error));
    }

    astraea::graphics::ShaderRegisterState shader_state{};
    astraea::graphics::ContextRegisterState context_state{};
    astraea::graphics::UserConfigRegisterState user_config_state{};

    std::optional<
        astraea::graphics::GraphicsIrSetInstanceCount>
        current_instances;
    std::optional<V3FirstSubmissionPacketSource>
        current_instances_source;

    std::optional<V3FirstSubmissionPacketSource>
        geometry_pgm_lo_source;
    std::optional<V3FirstSubmissionPacketSource>
        geometry_pgm_hi_source;
    std::optional<V3FirstSubmissionPacketSource>
        pixel_pgm_lo_source;
    std::optional<V3FirstSubmissionPacketSource>
        pixel_pgm_hi_source;

    std::optional<DrawSnapshot> draw_snapshot;

    for (std::size_t frame_index = 0;
         frame_index < framed->frames.size();
         ++frame_index) {
        const auto& frame =
            framed->frames[frame_index];

        if (frame.header.low_control_bits != 0U) {
            auto error =
                make_error(
                    V3FirstSubmissionErrorCode::
                        unsupported_type3_header_control_bits);
            error.frame_index = frame_index;
            error.word_offset = frame.word_offset;
            error.opcode = frame.header.opcode;
            error.type3_header_control_bits =
                frame.header.low_control_bits;
            return V3FirstSubmissionResult::failure(
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
                        V3FirstSubmissionErrorCode::
                            set_sh_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.set_sh_lowering_error =
                    lowered.error();
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }

            auto emission =
                std::move(lowered).value();
            auto applied =
                astraea::graphics::
                    apply_shader_register_graphics_ir(
                        emission,
                        shader_state);
            if (!applied.has_value()) {
                auto error =
                    make_error(
                        V3FirstSubmissionErrorCode::
                            shader_register_apply_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.shader_register_apply_error =
                    applied.error();
                return V3FirstSubmissionResult::failure(
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
                        V3FirstSubmissionErrorCode::
                            set_context_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.set_context_lowering_error =
                    lowered.error();
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }

            auto emission =
                std::move(lowered).value();
            auto applied =
                astraea::graphics::
                    apply_context_register_graphics_ir(
                        emission,
                        context_state);
            if (!applied.has_value()) {
                auto error =
                    make_error(
                        V3FirstSubmissionErrorCode::
                            context_register_apply_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.context_register_apply_error =
                    applied.error();
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }
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
                        V3FirstSubmissionErrorCode::
                            set_uconfig_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.set_uconfig_lowering_error =
                    lowered.error();
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }

            auto emission =
                std::move(lowered).value();
            auto applied =
                astraea::graphics::
                    apply_user_config_register_graphics_ir(
                        emission,
                        user_config_state);
            if (!applied.has_value()) {
                auto error =
                    make_error(
                        V3FirstSubmissionErrorCode::
                            user_config_register_apply_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.user_config_register_apply_error =
                    applied.error();
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }
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
                        V3FirstSubmissionErrorCode::
                            draw_control_lowering_failure);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                error.draw_control_lowering_error =
                    lowered.error();
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }

            auto emission =
                std::move(lowered).value();
            if (const auto* instances =
                    std::get_if<
                        astraea::graphics::
                            GraphicsIrSetInstanceCount>(
                        &emission.operation);
                instances != nullptr) {
                current_instances = *instances;
                current_instances_source =
                    V3FirstSubmissionPacketSource{
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
                        V3FirstSubmissionErrorCode::
                            unsupported_opcode);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }

            if (draw_snapshot.has_value()) {
                auto error =
                    make_error(
                        V3FirstSubmissionErrorCode::
                            multiple_draws);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                return V3FirstSubmissionResult::failure(
                    std::move(error));
            }

            if (!current_instances.has_value() ||
                !current_instances_source.has_value()) {
                auto error =
                    make_error(
                        V3FirstSubmissionErrorCode::
                            missing_instance_count_before_draw);
                error.frame_index = frame_index;
                error.word_offset = frame.word_offset;
                error.opcode = frame.header.opcode;
                return V3FirstSubmissionResult::failure(
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
                    .instances_source =
                        current_instances_source.value(),
                    .draw_source =
                        V3FirstSubmissionPacketSource{
                            .frame_index = frame_index,
                            .packet_word_offset =
                                frame.word_offset,
                            .value_word_offset =
                                frame.word_offset + 1U,
                        },
                };
            break;
        }

        default: {
            auto error =
                make_error(
                    V3FirstSubmissionErrorCode::
                        unsupported_opcode);
            error.frame_index = frame_index;
            error.word_offset = frame.word_offset;
            error.opcode = frame.header.opcode;
            return V3FirstSubmissionResult::failure(
                std::move(error));
        }
        }
    }

    if (!draw_snapshot.has_value()) {
        return V3FirstSubmissionResult::failure(
            make_error(
                V3FirstSubmissionErrorCode::
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
                V3FirstSubmissionErrorCode::
                    geometry_program_address_failure);
        error.geometry_program_address_error =
            geometry_address.error();
        return V3FirstSubmissionResult::failure(
            std::move(error));
    }

    auto pixel_address =
        astraea::graphics::
            resolve_pixel_program_address(
                snapshot.shader_register_state);
    if (!pixel_address.has_value()) {
        auto error =
            make_error(
                V3FirstSubmissionErrorCode::
                    pixel_program_address_failure);
        error.pixel_program_address_error =
            pixel_address.error();
        return V3FirstSubmissionResult::failure(
            std::move(error));
    }

    if (!snapshot.geometry_pgm_lo_source.has_value() ||
        !snapshot.geometry_pgm_hi_source.has_value() ||
        !snapshot.pixel_pgm_lo_source.has_value() ||
        !snapshot.pixel_pgm_hi_source.has_value()) {
        return V3FirstSubmissionResult::failure(
            make_error(
                V3FirstSubmissionErrorCode::
                    missing_program_register_provenance));
    }

    auto geometry_shader =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = geometry_address->value},
            astraea::graphics::AgcShaderStage::geometry);
    if (!geometry_shader.has_value()) {
        auto error =
            make_error(
                V3FirstSubmissionErrorCode::
                    geometry_shader_lookup_failure);
        error.created_shader_lookup_error =
            geometry_shader.error();
        return V3FirstSubmissionResult::failure(
            std::move(error));
    }

    auto pixel_shader =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = pixel_address->value},
            astraea::graphics::AgcShaderStage::pixel);
    if (!pixel_shader.has_value()) {
        auto error =
            make_error(
                V3FirstSubmissionErrorCode::
                    pixel_shader_lookup_failure);
        error.created_shader_lookup_error =
            pixel_shader.error();
        return V3FirstSubmissionResult::failure(
            std::move(error));
    }

    auto draw_plan =
        plan_v3_first_draw(
            snapshot.user_config_register_state,
            snapshot.instances,
            snapshot.draw);
    if (!draw_plan.has_value()) {
        auto error =
            make_error(
                V3FirstSubmissionErrorCode::
                    draw_plan_failure);
        error.draw_plan_error =
            draw_plan.error();
        return V3FirstSubmissionResult::failure(
            std::move(error));
    }

    return V3FirstSubmissionResult::success(
        V3FirstSubmissionPlan{
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
            .geometry_shader =
                &geometry_shader.value().get(),
            .pixel_shader =
                &pixel_shader.value().get(),
            .draw =
                draw_plan.value(),
            .geometry_pgm_lo_source =
                snapshot.geometry_pgm_lo_source.value(),
            .geometry_pgm_hi_source =
                snapshot.geometry_pgm_hi_source.value(),
            .pixel_pgm_lo_source =
                snapshot.pixel_pgm_lo_source.value(),
            .pixel_pgm_hi_source =
                snapshot.pixel_pgm_hi_source.value(),
            .instances_source =
                snapshot.instances_source,
            .draw_source =
                snapshot.draw_source,
        });
}

}  // namespace astraea::research
