#include <astraea/execution/sce_agc_submission_shader_binding.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace astraea::execution {
namespace {

[[nodiscard]] SceAgcSubmittedPixelShaderBindingError make_error(
    SceAgcSubmittedPixelShaderBindingErrorCode code) noexcept {
    auto error = SceAgcSubmittedPixelShaderBindingError{};
    error.code = code;
    return error;
}

void record_program_register_source(
    const astraea::graphics::GraphicsIrEmission& emission,
    std::size_t frame_index,
    std::uint16_t target_register,
    std::optional<SubmittedPixelProgramRegisterSource>& output) noexcept {
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
        SubmittedPixelProgramRegisterSource{
            .frame_index = frame_index,
            .packet_word_offset = packet_word_offset,
            .value_word_offset =
                packet_word_offset + 2U + value_index,
        };
}

}  // namespace

SceAgcSubmittedPixelShaderBindingResult
plan_sce_agc_submitted_pixel_shader_binding(
    const SceAgcDcbSubmission& submission,
    const CreatedAgcShaderRegistry& registry) {
    if (submission.flag != 0U) {
        auto error =
            make_error(
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    unsupported_submit_flag);
        error.submit_flag = submission.flag;
        return SceAgcSubmittedPixelShaderBindingResult::failure(
            std::move(error));
    }

    auto framed =
        astraea::graphics::frame_pm4_type3_stream(
            submission.command_buffer_bytes);
    if (!framed.has_value()) {
        auto error =
            make_error(
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    pm4_framing_failure);
        error.word_offset =
            framed.error().word_offset;
        error.pm4_framing_error =
            framed.error();
        return SceAgcSubmittedPixelShaderBindingResult::failure(
            std::move(error));
    }

    astraea::graphics::ShaderRegisterState register_state{};
    std::optional<SubmittedPixelProgramRegisterSource>
        pgm_lo_source;
    std::optional<SubmittedPixelProgramRegisterSource>
        pgm_hi_source;

    for (std::size_t frame_index = 0;
         frame_index < framed->frames.size();
         ++frame_index) {
        const auto& frame =
            framed->frames[frame_index];

        if (frame.header.low_control_bits != 0U) {
            auto error =
                make_error(
                    SceAgcSubmittedPixelShaderBindingErrorCode::
                        unsupported_type3_header_control_bits);
            error.frame_index = frame_index;
            error.word_offset = frame.word_offset;
            error.type3_header_control_bits =
                frame.header.low_control_bits;
            return SceAgcSubmittedPixelShaderBindingResult::failure(
                std::move(error));
        }

        auto lowered =
            astraea::graphics::
                lower_pm4_type3_frame_to_graphics_ir(
                    frame);
        if (!lowered.has_value()) {
            auto error =
                make_error(
                    SceAgcSubmittedPixelShaderBindingErrorCode::
                        pm4_lowering_failure);
            error.frame_index = frame_index;
            error.word_offset = frame.word_offset;
            error.pm4_lowering_error =
                lowered.error();
            return SceAgcSubmittedPixelShaderBindingResult::failure(
                std::move(error));
        }

        auto emission =
            std::move(lowered).value();
        auto applied =
            astraea::graphics::
                apply_shader_register_graphics_ir(
                    emission,
                    register_state);
        if (!applied.has_value()) {
            auto error =
                make_error(
                    SceAgcSubmittedPixelShaderBindingErrorCode::
                        shader_register_apply_failure);
            error.frame_index = frame_index;
            error.word_offset = frame.word_offset;
            error.shader_register_apply_error =
                applied.error();
            return SceAgcSubmittedPixelShaderBindingResult::failure(
                std::move(error));
        }

        record_program_register_source(
            emission,
            frame_index,
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo_source);
        record_program_register_source(
            emission,
            frame_index,
            astraea::graphics::
                kPixelProgramHiRegisterOffset,
            pgm_hi_source);
    }

    auto program_address =
        astraea::graphics::
            resolve_pixel_program_address(
                register_state);
    if (!program_address.has_value()) {
        auto error =
            make_error(
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    pixel_program_address_failure);
        error.pixel_program_address_error =
            program_address.error();
        return SceAgcSubmittedPixelShaderBindingResult::failure(
            std::move(error));
    }

    if (!pgm_lo_source.has_value() ||
        !pgm_hi_source.has_value()) {
        return SceAgcSubmittedPixelShaderBindingResult::failure(
            make_error(
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    missing_program_register_provenance));
    }

    auto shader =
        registry.lookup_unique(
            program_address.value());
    if (!shader.has_value()) {
        auto error =
            make_error(
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    created_shader_lookup_failure);
        error.created_shader_lookup_error =
            shader.error();
        return SceAgcSubmittedPixelShaderBindingResult::failure(
            std::move(error));
    }

    return SceAgcSubmittedPixelShaderBindingResult::success(
        SceAgcSubmittedPixelShaderBinding{
            .shader_register_state =
                std::move(register_state),
            .program_address =
                program_address.value(),
            .shader =
                &shader.value().get(),
            .pgm_lo_source =
                pgm_lo_source.value(),
            .pgm_hi_source =
                pgm_hi_source.value(),
        });
}

}  // namespace astraea::execution
