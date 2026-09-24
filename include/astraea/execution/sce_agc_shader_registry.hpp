#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/sce_agc_shader_preparation.hpp>
#include <astraea/graphics/agc_shader_binary.hpp>
#include <astraea/graphics/gpu_address.hpp>
#include <astraea/graphics/shader_program.hpp>
#include <astraea/graphics/shader_register_state.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

// One host-side record of a successfully validated/prepared AGC shader object.
//
// code_address is a backend-independent guest GPU virtual address. It is not a
// CPU guest pointer, a stage-specific submitted register address, or a Vulkan
// object. The original guest call addresses remain preserved separately as
// provenance.
struct CreatedAgcShader {
    astraea::graphics::GpuVirtualAddress code_address;
    astraea::graphics::AgcShaderStage stage =
        astraea::graphics::AgcShaderStage::pixel;
    SceAgcShaderPreparationProfile preparation_profile =
        SceAgcShaderPreparationProfile::v18_pixel_public_shape;
    astraea::memory::GuestAddress shader_handle;
    astraea::memory::GuestAddress shader_header_address;
    astraea::memory::GuestAddress shader_text_address;
    astraea::graphics::AgcShaderBinary shader;
    astraea::graphics::ShaderIrProgram shader_ir;
};

enum class CreatedAgcShaderMaterializationErrorCode {
    unsupported_preparation_profile,
    preparation_stage_mismatch,
    shader_ir_lowering_failure,
    host_allocation_failure,
};

struct CreatedAgcShaderMaterializationError {
    CreatedAgcShaderMaterializationErrorCode code =
        CreatedAgcShaderMaterializationErrorCode::
            unsupported_preparation_profile;
    std::optional<astraea::graphics::ShaderIrProgramError>
        shader_ir_error;

    auto operator<=>(
        const CreatedAgcShaderMaterializationError&) const = default;
};

using CreatedAgcShaderMaterializationResult =
    astraea::core::Result<
        CreatedAgcShader,
        CreatedAgcShaderMaterializationError>;

// Materializes one already validated supported preparation profile into a
// persistent host-side shader record without reading or mutating guest memory.
[[nodiscard]] CreatedAgcShaderMaterializationResult
materialize_created_agc_shader(
    const SceAgcShaderPreparationPlan& preparation);

enum class CreatedAgcShaderRegistrationErrorCode {
    host_allocation_failure,
    host_size_unrepresentable,
};

struct CreatedAgcShaderRegistrationError {
    CreatedAgcShaderRegistrationErrorCode code =
        CreatedAgcShaderRegistrationErrorCode::
            host_allocation_failure;

    auto operator<=>(
        const CreatedAgcShaderRegistrationError&) const = default;
};

using CreatedAgcShaderRegistrationResult =
    astraea::core::Result<
        std::size_t,
        CreatedAgcShaderRegistrationError>;

enum class CreatedAgcShaderLookupErrorCode {
    not_found,
    ambiguous,
};

struct CreatedAgcShaderLookupError {
    CreatedAgcShaderLookupErrorCode code =
        CreatedAgcShaderLookupErrorCode::not_found;
    astraea::graphics::PixelProgramGpuAddress program_address;
    std::size_t match_count = 0;
};

using CreatedAgcShaderLookupResult =
    astraea::core::Result<
        std::reference_wrapper<const CreatedAgcShader>,
        CreatedAgcShaderLookupError>;

enum class CreatedAgcShaderStageLookupErrorCode {
    not_found,
    ambiguous,
};

struct CreatedAgcShaderStageLookupError {
    CreatedAgcShaderStageLookupErrorCode code =
        CreatedAgcShaderStageLookupErrorCode::not_found;
    astraea::graphics::GpuVirtualAddress program_address;
    astraea::graphics::AgcShaderStage stage =
        astraea::graphics::AgcShaderStage::pixel;
    std::size_t match_count = 0;

    auto operator<=>(const CreatedAgcShaderStageLookupError&) const = default;
};

using CreatedAgcShaderStageLookupResult =
    astraea::core::Result<
        std::reference_wrapper<const CreatedAgcShader>,
        CreatedAgcShaderStageLookupError>;

enum class CreatedAgcShaderHandleLookupErrorCode {
    not_found,
    ambiguous,
};

struct CreatedAgcShaderHandleLookupError {
    CreatedAgcShaderHandleLookupErrorCode code =
        CreatedAgcShaderHandleLookupErrorCode::not_found;
    astraea::memory::GuestAddress shader_handle;
    std::size_t match_count = 0;

    auto operator<=>(
        const CreatedAgcShaderHandleLookupError&) const = default;
};

using CreatedAgcShaderHandleLookupResult =
    astraea::core::Result<
        std::reference_wrapper<const CreatedAgcShader>,
        CreatedAgcShaderHandleLookupError>;

class CreatedAgcShaderRegistry {
public:
    // Appends exactly one record. The returned logical insertion index remains
    // stable even if vector storage later moves.
    [[nodiscard]] CreatedAgcShaderRegistrationResult
    register_shader(CreatedAgcShader shader);

    // Transaction helper for the real create-shader HLE path. Removes only
    // the exact current final logical registration and never an earlier
    // record. No allocation is performed.
    [[nodiscard]] bool rollback_last_registration(
        std::size_t index) noexcept;

    // Compatibility lookup for the already-proven pixel submission path.
    // Only pixel-stage records participate. A Geometry record with the same
    // numeric code address cannot make a pixel lookup ambiguous.
    //
    // The returned reference remains valid only until the next registry
    // mutation.
    [[nodiscard]] CreatedAgcShaderLookupResult
    lookup_unique(
        astraea::graphics::PixelProgramGpuAddress
            program_address) const noexcept;

    // Generic stage-qualified GPU program identity. Identical numeric code
    // addresses in different stages never make one another ambiguous.
    [[nodiscard]] CreatedAgcShaderStageLookupResult
    lookup_unique_by_stage_and_code(
        astraea::graphics::GpuVirtualAddress program_address,
        astraea::graphics::AgcShaderStage stage) const noexcept;

    // Stage-independent identity used by later shader linkage. Duplicate
    // handles remain preserved and are reported explicitly as ambiguous.
    //
    // The returned reference remains valid only until the next registry
    // mutation.
    [[nodiscard]] CreatedAgcShaderHandleLookupResult
    lookup_unique_by_handle(
        astraea::memory::GuestAddress
            shader_handle) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return shaders_.size();
    }

    [[nodiscard]] const CreatedAgcShader* entry_at(
        std::size_t index) const noexcept;

private:
    std::vector<CreatedAgcShader> shaders_;
};

}  // namespace astraea::execution
