#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_program.hpp>
#include <astraea/graphics/shader_vector_execution.hpp>

namespace astraea::graphics {

struct SpirvVectorProbeOptions {
    ShaderWaveSize wave_size = ShaderWaveSize::unspecified;

    auto operator<=>(const SpirvVectorProbeOptions&) const = default;
};

struct SpirvVectorProbeLayout {
    std::uint32_t descriptor_set = 0;
    std::uint32_t binding = 0;
    std::uint32_t word_size_bytes = 4;
    std::uint32_t wave_size = 0;
    std::uint32_t vgpr_stride_words = 0;
    std::uint32_t required_vgpr_count = 0;
    std::uint32_t required_state_word_count = 0;

    auto operator<=>(const SpirvVectorProbeLayout&) const = default;
};

struct SpirvVectorProbeModule {
    std::vector<std::uint32_t> words;
    SpirvVectorProbeLayout layout;

    auto operator<=>(const SpirvVectorProbeModule&) const = default;
};

enum class SpirvVectorProbeErrorCode {
    invalid_wave_size,
    missing_end_program,
    duplicate_end_program,
    operation_after_end_program,
    unsupported_operation,
    host_allocation_failure,
};

struct SpirvVectorProbeError {
    SpirvVectorProbeErrorCode code =
        SpirvVectorProbeErrorCode::unsupported_operation;
    std::size_t emission_index = 0;
    std::optional<ShaderIrUnsupportedReason> unsupported_reason;

    auto operator<=>(const SpirvVectorProbeError&) const = default;
};

using SpirvVectorProbeResult =
    astraea::core::Result<
        SpirvVectorProbeModule,
        SpirvVectorProbeError>;

// Lowers one intentionally narrow, straight-line Shader IR profile to a
// deterministic SPIR-V compute module suitable for Vulkan 1.3 validation.
//
// This is a compiler/execution probe ABI, not a guest resource mapping:
// VGPR words are flattened into one storage buffer as
//     vgpr_index * wave_size + lane
// and every lane is treated as active. Real AGC resource/IO semantics and
// partial EXEC masks remain separate dependencies.
[[nodiscard]] SpirvVectorProbeResult
lower_shader_ir_to_spirv_vector_probe(
    const ShaderIrProgram& program,
    SpirvVectorProbeOptions options);

}  // namespace astraea::graphics
