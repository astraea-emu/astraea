#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/rdna2_decoder.hpp>
#include <astraea/graphics/shader_ir.hpp>

namespace astraea::graphics {

struct ShaderIrProgram {
    std::size_t source_word_count = 0;
    std::vector<ShaderIrEmission> emissions;
};

enum class ShaderIrProgramErrorCode {
    decode_failure,
    invalid_instruction_extent,
    host_allocation_failure,
};

struct ShaderIrProgramError {
    ShaderIrProgramErrorCode code =
        ShaderIrProgramErrorCode::host_allocation_failure;
    std::size_t word_index = 0;
    std::size_t lowered_instruction_count = 0;
    std::optional<Rdna2DecodeError> decode_error;

    auto operator<=>(const ShaderIrProgramError&) const = default;
};

using ShaderIrProgramResult =
    astraea::core::Result<
        ShaderIrProgram,
        ShaderIrProgramError>;

// Decodes and lowers an already-extracted, bounded generic RDNA2 instruction
// stream in linear source order. Decoded instruction word_count controls stream
// advancement, so extension dwords remain part of their owning instruction.
//
// This function does not follow branches, construct a CFG, execute wave state,
// parse Sony shader containers, or lower to SPIR-V/Vulkan.
[[nodiscard]] ShaderIrProgramResult
lower_rdna2_stream_to_shader_ir(
    std::span<const std::uint32_t> words);

}  // namespace astraea::graphics
