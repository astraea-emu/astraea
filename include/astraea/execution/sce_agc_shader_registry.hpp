#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/sce_agc_shader_preparation.hpp>
#include <astraea/graphics/agc_shader_binary.hpp>
#include <astraea/graphics/shader_program.hpp>
#include <astraea/graphics/shader_register_state.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

// One host-side record of a successfully validated/prepared AGC shader object.
//
// The program address is typed in the GPU domain because it is the value later
// reconstructed from the evidence-scoped pixel PGM_LO/PGM_HI register pair.
// The original guest addresses remain preserved separately as provenance.
struct CreatedAgcShader {
    astraea::graphics::PixelProgramGpuAddress program_address;
    astraea::memory::GuestAddress shader_handle;
    astraea::memory::GuestAddress shader_header_address;
    astraea::memory::GuestAddress shader_text_address;
    astraea::graphics::AgcShaderBinary shader;
    astraea::graphics::ShaderIrProgram shader_ir;
};

enum class CreatedAgcShaderMaterializationErrorCode {
    unsupported_preparation_profile,
    shader_ir_lowering_failure,
    host_allocation_failure,
};

struct CreatedAgcShaderMaterializationError {
    CreatedAgcShaderMaterializationErrorCode code =
        CreatedAgcShaderMaterializationErrorCode::
            unsupported_preparation_profile;
    std::optional<astraea::graphics::ShaderIrProgramError>
        shader_ir_error;
};

using CreatedAgcShaderMaterializationResult =
    astraea::core::Result<
        CreatedAgcShader,
        CreatedAgcShaderMaterializationError>;

// Materializes the existing validated V1 pixel preparation result into one
// host-side shader record without reading or mutating guest memory.
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

class CreatedAgcShaderRegistry {
public:
    // Appends exactly one record. The returned logical insertion index remains
    // stable even if vector storage later moves.
    [[nodiscard]] CreatedAgcShaderRegistrationResult
    register_shader(CreatedAgcShader shader);

    // Returns a record only when the requested program address identifies
    // exactly one created object. Duplicate program addresses remain
    // preserved and are reported as ambiguous rather than overwritten.
    //
    // The returned reference remains valid only until the next registry
    // mutation.
    [[nodiscard]] CreatedAgcShaderLookupResult
    lookup_unique(
        astraea::graphics::PixelProgramGpuAddress
            program_address) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return shaders_.size();
    }

    [[nodiscard]] const CreatedAgcShader* entry_at(
        std::size_t index) const noexcept;

private:
    std::vector<CreatedAgcShader> shaders_;
};

}  // namespace astraea::execution
