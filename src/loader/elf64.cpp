#include <astraea/loader/elf64.hpp>

#include <array>
#include <bit>
#include <limits>
#include <type_traits>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::size_t kIdentSize = 16;
constexpr std::size_t kElf64HeaderSize = 64;
constexpr std::size_t kElf64ProgramHeaderSize = 56;

constexpr std::uint8_t kElfClass64 = 2;
constexpr std::uint8_t kElfDataLittleEndian = 1;
constexpr std::uint8_t kElfCurrentVersion = 1;

constexpr std::uint16_t kEtExec = 2;
constexpr std::uint16_t kEtDyn = 3;
constexpr std::uint16_t kEmX86_64 = 62;
constexpr std::uint16_t kPnXnum = 0xffff;

constexpr std::uint32_t kPtLoad = 1;

constexpr std::array<std::byte, 4> kElfMagic{
    std::byte{0x7f},
    std::byte{'E'},
    std::byte{'L'},
    std::byte{'F'},
};

template <typename T>
[[nodiscard]] T read_little_endian(std::span<const std::byte> bytes, std::size_t offset) {
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(std::uint64_t));

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto byte = static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(bytes[offset + i]));
        value |= byte << (i * 8U);
    }
    return static_cast<T>(value);
}

[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result) {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool checked_multiply(
    std::uint64_t lhs,
    std::uint64_t rhs,
    std::uint64_t& result) {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] std::uint64_t input_size_u64(std::span<const std::byte> bytes) {
    if constexpr (sizeof(std::size_t) <= sizeof(std::uint64_t)) {
        return static_cast<std::uint64_t>(bytes.size());
    } else {
        const auto max = static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max());
        return bytes.size() > max ? std::numeric_limits<std::uint64_t>::max()
                                  : static_cast<std::uint64_t>(bytes.size());
    }
}

[[nodiscard]] ElfError header_error(ElfErrorCode code, std::uint64_t offset) {
    return ElfError{code, std::nullopt, offset};
}

[[nodiscard]] ElfError program_header_error(
    ElfErrorCode code,
    std::size_t index,
    std::uint64_t offset) {
    return ElfError{code, index, offset};
}

[[nodiscard]] bool is_power_of_two(std::uint64_t value) {
    return value != 0 && (value & (value - 1U)) == 0;
}

}  // namespace

ElfParseResult parse_elf64(std::span<const std::byte> bytes) {
    if (bytes.size() < kIdentSize) {
        return ElfParseResult::failure(header_error(ElfErrorCode::file_too_small, 0));
    }

    for (std::size_t i = 0; i < kElfMagic.size(); ++i) {
        if (bytes[i] != kElfMagic[i]) {
            return ElfParseResult::failure(header_error(ElfErrorCode::bad_magic, i));
        }
    }

    if (std::to_integer<std::uint8_t>(bytes[4]) != kElfClass64) {
        return ElfParseResult::failure(header_error(ElfErrorCode::unsupported_class, 4));
    }
    if (std::to_integer<std::uint8_t>(bytes[5]) != kElfDataLittleEndian) {
        return ElfParseResult::failure(header_error(ElfErrorCode::unsupported_endianness, 5));
    }
    if (std::to_integer<std::uint8_t>(bytes[6]) != kElfCurrentVersion) {
        return ElfParseResult::failure(header_error(ElfErrorCode::unsupported_ident_version, 6));
    }

    if (bytes.size() < kElf64HeaderSize) {
        return ElfParseResult::failure(header_error(ElfErrorCode::file_too_small, kIdentSize));
    }

    ElfHeader header{
        .os_abi = std::to_integer<std::uint8_t>(bytes[7]),
        .abi_version = std::to_integer<std::uint8_t>(bytes[8]),
        .type = read_little_endian<std::uint16_t>(bytes, 16),
        .machine = read_little_endian<std::uint16_t>(bytes, 18),
        .version = read_little_endian<std::uint32_t>(bytes, 20),
        .entry = read_little_endian<std::uint64_t>(bytes, 24),
        .program_header_offset = read_little_endian<std::uint64_t>(bytes, 32),
        .section_header_offset = read_little_endian<std::uint64_t>(bytes, 40),
        .flags = read_little_endian<std::uint32_t>(bytes, 48),
        .header_size = read_little_endian<std::uint16_t>(bytes, 52),
        .program_header_entry_size = read_little_endian<std::uint16_t>(bytes, 54),
        .program_header_count = read_little_endian<std::uint16_t>(bytes, 56),
        .section_header_entry_size = read_little_endian<std::uint16_t>(bytes, 58),
        .section_header_count = read_little_endian<std::uint16_t>(bytes, 60),
        .section_name_table_index = read_little_endian<std::uint16_t>(bytes, 62),
    };

    if (header.version != kElfCurrentVersion) {
        return ElfParseResult::failure(header_error(ElfErrorCode::unsupported_elf_version, 20));
    }
    if (header.machine != kEmX86_64) {
        return ElfParseResult::failure(header_error(ElfErrorCode::unsupported_machine, 18));
    }
    if (header.type != kEtExec && header.type != kEtDyn) {
        return ElfParseResult::failure(header_error(ElfErrorCode::unsupported_file_type, 16));
    }
    if (header.header_size != kElf64HeaderSize) {
        return ElfParseResult::failure(header_error(ElfErrorCode::invalid_elf_header_size, 52));
    }
    if (header.program_header_count == kPnXnum) {
        return ElfParseResult::failure(
            header_error(ElfErrorCode::unsupported_extended_program_header_count, 56));
    }

    if (header.program_header_count == 0) {
        return ElfParseResult::success(ElfImage{.header = header, .program_headers = {}});
    }

    if (header.program_header_entry_size != kElf64ProgramHeaderSize) {
        return ElfParseResult::failure(
            header_error(ElfErrorCode::invalid_program_header_entry_size, 54));
    }

    std::uint64_t table_size = 0;
    if (!checked_multiply(
            header.program_header_count,
            header.program_header_entry_size,
            table_size)) {
        return ElfParseResult::failure(header_error(ElfErrorCode::integer_overflow, 56));
    }

    std::uint64_t table_end = 0;
    if (!checked_add(header.program_header_offset, table_size, table_end)) {
        return ElfParseResult::failure(header_error(ElfErrorCode::integer_overflow, 32));
    }

    const auto input_size = input_size_u64(bytes);
    if (header.program_header_offset > input_size || table_end > input_size) {
        return ElfParseResult::failure(
            header_error(ElfErrorCode::program_header_table_out_of_bounds, 32));
    }

    std::vector<ProgramHeader> program_headers;
    program_headers.reserve(header.program_header_count);

    std::optional<std::uint64_t> previous_load_address;

    for (std::size_t index = 0; index < header.program_header_count; ++index) {
        const auto entry_offset_u64 =
            header.program_header_offset +
            static_cast<std::uint64_t>(index) * kElf64ProgramHeaderSize;
        const auto entry_offset = static_cast<std::size_t>(entry_offset_u64);

        ProgramHeader program_header{
            .type = read_little_endian<std::uint32_t>(bytes, entry_offset),
            .flags = read_little_endian<std::uint32_t>(bytes, entry_offset + 4),
            .offset = read_little_endian<std::uint64_t>(bytes, entry_offset + 8),
            .virtual_address = read_little_endian<std::uint64_t>(bytes, entry_offset + 16),
            .physical_address = read_little_endian<std::uint64_t>(bytes, entry_offset + 24),
            .file_size = read_little_endian<std::uint64_t>(bytes, entry_offset + 32),
            .memory_size = read_little_endian<std::uint64_t>(bytes, entry_offset + 40),
            .alignment = read_little_endian<std::uint64_t>(bytes, entry_offset + 48),
            .index = index,
        };

        if (program_header.type == kPtLoad) {
            if (program_header.file_size > program_header.memory_size) {
                return ElfParseResult::failure(program_header_error(
                    ElfErrorCode::load_file_size_exceeds_memory_size,
                    index,
                    entry_offset_u64));
            }

            if (program_header.file_size > 0) {
                std::uint64_t file_end = 0;
                if (!checked_add(program_header.offset, program_header.file_size, file_end)) {
                    return ElfParseResult::failure(program_header_error(
                        ElfErrorCode::integer_overflow,
                        index,
                        entry_offset_u64 + 8));
                }
                if (program_header.offset > input_size || file_end > input_size) {
                    return ElfParseResult::failure(program_header_error(
                        ElfErrorCode::segment_file_range_out_of_bounds,
                        index,
                        entry_offset_u64 + 8));
                }
            }

            if (program_header.memory_size > 0) {
                const auto last_offset = program_header.memory_size - 1U;
                if (program_header.virtual_address >
                    std::numeric_limits<std::uint64_t>::max() - last_offset) {
                    return ElfParseResult::failure(program_header_error(
                        ElfErrorCode::load_virtual_range_overflow,
                        index,
                        entry_offset_u64 + 16));
                }
            }

            if (program_header.alignment > 1) {
                if (!is_power_of_two(program_header.alignment) ||
                    program_header.virtual_address % program_header.alignment !=
                        program_header.offset % program_header.alignment) {
                    return ElfParseResult::failure(program_header_error(
                        ElfErrorCode::invalid_load_alignment,
                        index,
                        entry_offset_u64 + 48));
                }
            }

            if (previous_load_address.has_value() &&
                program_header.virtual_address < *previous_load_address) {
                return ElfParseResult::failure(program_header_error(
                    ElfErrorCode::load_segments_out_of_order,
                    index,
                    entry_offset_u64 + 16));
            }
            previous_load_address = program_header.virtual_address;
        }

        program_headers.push_back(program_header);
    }

    return ElfParseResult::success(ElfImage{
        .header = header,
        .program_headers = std::move(program_headers),
    });
}

}  // namespace astraea::loader
