#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::graphics {

enum class AgcShaderStage {
    compute,
    pixel,
    geometry,
    hull,
    geometry_front,
    hull_front,
    geometry_back,
    hull_back,
    function,
};

struct AgcShaderProgramType {
    std::uint8_t raw = 0;
    std::optional<AgcShaderStage> known;

    auto operator<=>(const AgcShaderProgramType&) const = default;
};

struct AgcShaderSectionProvenance {
    std::size_t section_index = 0;
    std::uint64_t file_offset = 0;
    std::uint64_t file_size = 0;
    std::uint64_t alignment = 0;

    auto operator<=>(const AgcShaderSectionProvenance&) const = default;
};

struct AgcShaderContainer {
    std::uint8_t os_abi = 0;
    std::uint8_t abi_version = 0;
    std::uint16_t elf_type = 0;
    std::uint16_t elf_machine = 0;
    std::uint32_t elf_flags = 0;

    std::uint32_t header_magic = 0;
    std::uint32_t header_version = 0;
    std::uint32_t declared_header_size = 0;
    std::uint32_t declared_shader_text_size = 0;
    AgcShaderProgramType program_type{};

    AgcShaderSectionProvenance shader_header_section{};
    AgcShaderSectionProvenance shader_text_section{};

    std::uint32_t program_byte_size = 0;
    std::uint32_t trailer_sl00_byte_size = 0;

    // Full input bytes and both AGC sections remain available as opaque
    // provenance. Unknown header/trailer fields are intentionally not assigned
    // semantics merely because they are present in a public sample.
    std::vector<std::byte> container_bytes;
    std::vector<std::byte> shader_header_bytes;
    std::vector<std::byte> shader_text_bytes;

    // Only the evidence-bounded program prefix is converted to generic RDNA2
    // dwords. The remaining shader-text bytes stay opaque above.
    std::vector<std::uint32_t> rdna2_words;
};

enum class AgcShaderContainerErrorCode {
    file_too_small,
    bad_magic,
    unsupported_class,
    unsupported_endianness,
    unsupported_ident_version,
    unsupported_elf_version,
    unsupported_machine,
    invalid_elf_header_size,
    missing_section_table,
    unsupported_extended_section_count,
    invalid_section_header_entry_size,
    invalid_section_name_table_index,
    integer_overflow,
    section_header_table_out_of_bounds,
    invalid_section_name_table,
    section_out_of_bounds,
    section_name_out_of_bounds,
    unterminated_section_name,
    missing_shader_header,
    duplicate_shader_header,
    missing_shader_text,
    duplicate_shader_text,
    invalid_shader_section_type,
    shader_header_too_small,
    bad_shader_header_magic,
    declared_header_size_mismatch,
    declared_shader_text_size_mismatch,
    shader_text_too_small_for_trailer,
    program_extent_out_of_bounds,
    program_size_not_dword_aligned,
    host_allocation_failure,
};

struct AgcShaderContainerError {
    AgcShaderContainerErrorCode code =
        AgcShaderContainerErrorCode::file_too_small;
    std::optional<std::size_t> section_index;
    std::uint64_t file_offset = 0;

    auto operator<=>(const AgcShaderContainerError&) const = default;
};

using AgcShaderContainerResult =
    astraea::core::Result<
        AgcShaderContainer,
        AgcShaderContainerError>;

// Parses only the evidence-backed outer envelope used by current public PS5
// AGC shader-container examples:
//
//   ELF64 little-endian / EM_AMDGPU
//       .shader_header
//       .shader_text
//
// The parser validates the documented AGC header magic and declared section
// sizes, then uses the documented shader-text trailer program-length field to
// expose a bounded little-endian RDNA2 dword stream. It deliberately does not
// decode register lists, resources, descriptors, hashes, launch state, or any
// other unknown AGC header/trailer field.
[[nodiscard]] AgcShaderContainerResult
parse_agc_shader_container(std::span<const std::byte> bytes);

}  // namespace astraea::graphics
