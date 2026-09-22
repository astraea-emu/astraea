#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/agc_shader_binary.hpp>

namespace astraea::graphics {

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

    AgcShaderSectionProvenance shader_header_section{};
    AgcShaderSectionProvenance shader_text_section{};

    // The runtime header/text semantics are canonicalized here rather than
    // duplicated in the outer ELF/container parser.
    AgcShaderBinary shader;

    // Full container bytes remain available as outer-envelope provenance.
    std::vector<std::byte> container_bytes;
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
    register_table_extent_overflow,
    register_table_extent_out_of_bounds,
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
    std::optional<AgcShaderBinaryError> shader_binary_error;

    auto operator<=>(const AgcShaderContainerError&) const = default;
};

using AgcShaderContainerResult =
    astraea::core::Result<
        AgcShaderContainer,
        AgcShaderContainerError>;

// Parses only the evidence-backed outer ELF envelope used by current public
// PS5 AGC shader-container examples, then delegates the .shader_header /
// .shader_text pair to parse_agc_shader_binary().
//
// The outer parser does not decode register semantics, resources, descriptors,
// launch state, hashes, or other unknown AGC fields.
[[nodiscard]] AgcShaderContainerResult
parse_agc_shader_container(std::span<const std::byte> bytes);

}  // namespace astraea::graphics
