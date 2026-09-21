#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::loader {

enum class ElfParseProfile {
    generic,
    ps5_sce,
};

enum class ElfErrorCode {
    file_too_small,
    bad_magic,
    unsupported_class,
    unsupported_endianness,
    unsupported_ident_version,
    unsupported_elf_version,
    unsupported_machine,
    unsupported_file_type,
    invalid_elf_header_size,
    invalid_program_header_entry_size,
    unsupported_extended_program_header_count,
    integer_overflow,
    program_header_table_out_of_bounds,
    segment_file_range_out_of_bounds,
    load_file_size_exceeds_memory_size,
    load_virtual_range_overflow,
    invalid_load_alignment,
    load_segments_out_of_order,
};

struct ElfError {
    ElfErrorCode code;
    std::optional<std::size_t> program_header_index;
    std::uint64_t file_offset;
};

struct ElfHeader {
    std::uint8_t os_abi;
    std::uint8_t abi_version;
    std::uint16_t type;
    std::uint16_t machine;
    std::uint32_t version;
    std::uint64_t entry;
    std::uint64_t program_header_offset;
    std::uint64_t section_header_offset;
    std::uint32_t flags;
    std::uint16_t header_size;
    std::uint16_t program_header_entry_size;
    std::uint16_t program_header_count;
    std::uint16_t section_header_entry_size;
    std::uint16_t section_header_count;
    std::uint16_t section_name_table_index;
};

struct ProgramHeader {
    std::uint32_t type;
    std::uint32_t flags;
    std::uint64_t offset;
    std::uint64_t virtual_address;
    std::uint64_t physical_address;
    std::uint64_t file_size;
    std::uint64_t memory_size;
    std::uint64_t alignment;
    std::size_t index;
};

struct ElfImage {
    ElfHeader header;
    std::vector<ProgramHeader> program_headers;
};

using ElfParseResult = astraea::core::Result<ElfImage, ElfError>;

[[nodiscard]] ElfParseResult parse_elf64(
    std::span<const std::byte> bytes,
    ElfParseProfile profile = ElfParseProfile::generic);

}  // namespace astraea::loader
