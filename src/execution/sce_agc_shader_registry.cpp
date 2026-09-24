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

[[nodiscard]] CreatedAgcShaderStageLookupError
stage_lookup_error(
    CreatedAgcShaderStageLookupErrorCode code,
    astraea::graphics::GpuVirtualAddress program_address,
    astraea::graphics::AgcShaderStage stage,
    std::size_t match_count) noexcept {
    return CreatedAgcShaderStageLookupError{
        .code = code,
        .program_address = program_address,
        .stage = stage,
        .match_count = match_count,
    };
}

[[nodiscard]] CreatedAgcShaderHandleLookupError
handle_lookup_error(
    CreatedAgcShaderHandleLookupErrorCode code,
    astraea::memory::GuestAddress shader_handle,
    std::size_t match_count) noexcept {
    return CreatedAgcShaderHandleLookupError{
        .code = code,
        .shader_handle = shader_handle,
        .match_count = match_count,
    };
}

}  // namespace

CreatedAgcShaderMaterializationResult
materialize_created_agc_shader(
    const SceAgcShaderPreparationPlan& preparation) {
    using Stage = astraea::graphics::AgcShaderStage;

    const auto& shader = preparation.create_shader.shader;
    Stage stage = Stage::pixel;

    switch (preparation.profile) {
    case SceAgcShaderPreparationProfile::
        v18_pixel_public_shape:
        if (shader.program_type.raw != 1U ||
            shader.program_type.known !=
                std::optional<Stage>{Stage::pixel}) {
            return CreatedAgcShaderMaterializationResult::failure(
                materialization_error(
                    CreatedAgcShaderMaterializationErrorCode::
                        preparation_stage_mismatch));
        }
        stage = Stage::pixel;
        break;

    case SceAgcShaderPreparationProfile::
        v18_geometry_es_public_shape:
        if (shader.program_type.raw != 2U ||
            shader.program_type.known !=
                std::optional<Stage>{Stage::geometry}) {
            return CreatedAgcShaderMaterializationResult::failure(
                materialization_error(
                    CreatedAgcShaderMaterializationErrorCode::
                        preparation_stage_mismatch));
        }
        stage = Stage::geometry;
        break;

    default:
        return CreatedAgcShaderMaterializationResult::failure(
            materialization_error(
                CreatedAgcShaderMaterializationErrorCode::
                    unsupported_preparation_profile));
    }

    auto shader_ir =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(
                shader.rdna2_words);
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
                .code_address =
                    astraea::graphics::GpuVirtualAddress{
                        .value =
                            preparation
                                .create_shader
                                .request
                                .shader_text_address
                                .value(),
                    },
                .stage = stage,
                .preparation_profile =
                    preparation.profile,
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
                .shader = shader,
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

bool CreatedAgcShaderRegistry::rollback_last_registration(
    std::size_t index) noexcept {
    if (shaders_.empty() ||
        index != shaders_.size() - 1U) {
        return false;
    }

    shaders_.pop_back();
    return true;
}

CreatedAgcShaderLookupResult
CreatedAgcShaderRegistry::lookup_unique(
    astraea::graphics::PixelProgramGpuAddress
        program_address) const noexcept {
    const CreatedAgcShader* match = nullptr;
    std::size_t match_count = 0;

    for (const auto& shader : shaders_) {
        if (shader.stage !=
                astraea::graphics::AgcShaderStage::pixel ||
            shader.code_address.value !=
                program_address.value) {
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

CreatedAgcShaderStageLookupResult
CreatedAgcShaderRegistry::lookup_unique_by_stage_and_code(
    astraea::graphics::GpuVirtualAddress program_address,
    astraea::graphics::AgcShaderStage stage) const noexcept {
    const CreatedAgcShader* match = nullptr;
    std::size_t match_count = 0;

    for (const auto& shader : shaders_) {
        if (shader.stage != stage ||
            shader.code_address != program_address) {
            continue;
        }

        ++match_count;
        if (match_count == 1U) {
            match = &shader;
        }
    }

    if (match_count == 0U || match == nullptr) {
        return CreatedAgcShaderStageLookupResult::failure(
            stage_lookup_error(
                CreatedAgcShaderStageLookupErrorCode::not_found,
                program_address,
                stage,
                0U));
    }

    if (match_count != 1U) {
        return CreatedAgcShaderStageLookupResult::failure(
            stage_lookup_error(
                CreatedAgcShaderStageLookupErrorCode::ambiguous,
                program_address,
                stage,
                match_count));
    }

    return CreatedAgcShaderStageLookupResult::success(
        std::cref(*match));
}

CreatedAgcShaderHandleLookupResult
CreatedAgcShaderRegistry::lookup_unique_by_handle(
    astraea::memory::GuestAddress
        shader_handle) const noexcept {
    const CreatedAgcShader* match = nullptr;
    std::size_t match_count = 0;

    for (const auto& shader : shaders_) {
        if (shader.shader_handle != shader_handle) {
            continue;
        }

        ++match_count;
        if (match_count == 1) {
            match = &shader;
        }
    }

    if (match_count == 0 || match == nullptr) {
        return CreatedAgcShaderHandleLookupResult::failure(
            handle_lookup_error(
                CreatedAgcShaderHandleLookupErrorCode::
                    not_found,
                shader_handle,
                0));
    }

    if (match_count != 1) {
        return CreatedAgcShaderHandleLookupResult::failure(
            handle_lookup_error(
                CreatedAgcShaderHandleLookupErrorCode::
                    ambiguous,
                shader_handle,
                match_count));
    }

    return CreatedAgcShaderHandleLookupResult::success(
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
