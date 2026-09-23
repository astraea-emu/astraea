#include <astraea/execution/sce_agc_shader_registry.hpp>

#include <cstddef>
#include <functional>
#include <new>
#include <stdexcept>
#include <utility>

namespace astraea::execution {
namespace {

[[nodiscard]] CreatedAgcShaderMaterializationError
materialization_error(
    CreatedAgcShaderMaterializationErrorCode code,
    std::optional<astraea::graphics::ShaderIrProgramError>
        shader_ir_error = std::nullopt) {
    return CreatedAgcShaderMaterializationError{
        .code = code,
        .shader_ir_error = std::move(shader_ir_error),
    };
}

[[nodiscard]] CreatedAgcShaderLookupError lookup_error(
    CreatedAgcShaderLookupErrorCode code,
    astraea::graphics::PixelProgramGpuAddress program_address,
    std::size_t match_count) noexcept {
    return CreatedAgcShaderLookupError{
        .code = code,
        .program_address = program_address,
        .match_count = match_count,
    };
}

}  // namespace

CreatedAgcShaderMaterializationResult
materialize_created_agc_shader(
    const SceAgcShaderPreparationPlan& preparation) {
    if (preparation.profile !=
        SceAgcShaderPreparationProfile::
            v18_pixel_public_shape) {
        return CreatedAgcShaderMaterializationResult::failure(
            materialization_error(
                CreatedAgcShaderMaterializationErrorCode::
                    unsupported_preparation_profile));
    }

    auto shader_ir =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(
                preparation.create_shader.shader.rdna2_words);
    if (!shader_ir.has_value()) {
        return CreatedAgcShaderMaterializationResult::failure(
            materialization_error(
                CreatedAgcShaderMaterializationErrorCode::
                    shader_ir_lowering_failure,
                shader_ir.error()));
    }

    try {
        return CreatedAgcShaderMaterializationResult::success(
            CreatedAgcShader{
                .program_address =
                    astraea::graphics::
                        PixelProgramGpuAddress{
                            .value =
                                preparation
                                    .create_shader
                                    .request
                                    .shader_text_address
                                    .value(),
                        },
                .shader_handle =
                    preparation.shader_handle,
                .shader_header_address =
                    preparation
                        .create_shader
                        .request
                        .shader_header_address,
                .shader_text_address =
                    preparation
                        .create_shader
                        .request
                        .shader_text_address,
                .shader =
                    preparation.create_shader.shader,
                .shader_ir =
                    std::move(shader_ir).value(),
            });
    } catch (const std::bad_alloc&) {
        return CreatedAgcShaderMaterializationResult::failure(
            materialization_error(
                CreatedAgcShaderMaterializationErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return CreatedAgcShaderMaterializationResult::failure(
            materialization_error(
                CreatedAgcShaderMaterializationErrorCode::
                    host_allocation_failure));
    }
}

CreatedAgcShaderRegistrationResult
CreatedAgcShaderRegistry::register_shader(
    CreatedAgcShader shader) {
    const auto index = shaders_.size();
    if (index == shaders_.max_size()) {
        return CreatedAgcShaderRegistrationResult::failure(
            CreatedAgcShaderRegistrationError{
                .code =
                    CreatedAgcShaderRegistrationErrorCode::
                        host_size_unrepresentable,
            });
    }

    try {
        shaders_.push_back(std::move(shader));
        return CreatedAgcShaderRegistrationResult::success(
            index);
    } catch (const std::bad_alloc&) {
        return CreatedAgcShaderRegistrationResult::failure(
            CreatedAgcShaderRegistrationError{
                .code =
                    CreatedAgcShaderRegistrationErrorCode::
                        host_allocation_failure,
            });
    } catch (const std::length_error&) {
        return CreatedAgcShaderRegistrationResult::failure(
            CreatedAgcShaderRegistrationError{
                .code =
                    CreatedAgcShaderRegistrationErrorCode::
                        host_size_unrepresentable,
            });
    }
}

CreatedAgcShaderLookupResult
CreatedAgcShaderRegistry::lookup_unique(
    astraea::graphics::PixelProgramGpuAddress
        program_address) const noexcept {
    const CreatedAgcShader* match = nullptr;
    std::size_t match_count = 0;

    for (const auto& shader : shaders_) {
        if (shader.program_address != program_address) {
            continue;
        }

        ++match_count;
        if (match_count == 1) {
            match = &shader;
        }
    }

    if (match_count == 0 || match == nullptr) {
        return CreatedAgcShaderLookupResult::failure(
            lookup_error(
                CreatedAgcShaderLookupErrorCode::
                    not_found,
                program_address,
                0));
    }

    if (match_count != 1) {
        return CreatedAgcShaderLookupResult::failure(
            lookup_error(
                CreatedAgcShaderLookupErrorCode::
                    ambiguous,
                program_address,
                match_count));
    }

    return CreatedAgcShaderLookupResult::success(
        std::cref(*match));
}

const CreatedAgcShader*
CreatedAgcShaderRegistry::entry_at(
    std::size_t index) const noexcept {
    if (index >= shaders_.size()) {
        return nullptr;
    }
    return &shaders_[index];
}

}  // namespace astraea::execution
