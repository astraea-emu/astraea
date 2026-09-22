#include <astraea/graphics/agc_shader_container.hpp>

#include <array>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace astraea::graphics {
namespace {

constexpr std::size_t kElf64HeaderSize = 64;
constexpr std::size_t kElf64SectionHeaderSize = 64;
constexpr std::uint8_t kElfClass64 = 2;
constexpr std::uint8_t kElfDataLittleEndian = 1;
constexpr std::uint8_t kElfCurrentVersion = 1;
constexpr std::uint16_t kEmAmdgpu = 224;
constexpr std::uint16_t kShnXindex = 0xffff;
constexpr std::uint32_t kShtProgbits = 1;
constexpr std::uint32_t kShtStrtab = 3;
constexpr std::uint32_t kShtNobits = 8;

constexpr std::uint32_t kAgcHeaderMagic = 0x34333231U;
constexpr std::size_t kAgcHeaderMinimumSize = 96;
constexpr std::size_t kAgcHeaderSizeOffset = 0x40;
constexpr std::size_t kAgcShaderTextSizeOffset = 0x44;
constexpr std::size_t kAgcProgramTypeOffset = 0x5a;

constexpr std::size_t kAgcShaderTextTrailerSize = 0x30;
constexpr std::size_t kAgcTrailerProgramLengthOffset = 0x14;
constexpr std::size_t kAgcTrailerSl00LengthOffset = 0x1c;

constexpr std::array<std::byte, 4> kElfMagic{
    std::byte{0x7f},
    std::byte{'E'},
    std::byte{'L'},
    std::byte{'F'},
};

struct SectionRecord {
    std::size_t index = 0;
    std::uint32_t name_index = 0;
    std::uint32_t type = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t alignment = 0;
};

template <typename T>
[[nodiscard]] T read_little_endian(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(std::uint64_t));

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto byte =
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    bytes[offset + i]));
        value |= byte << (i * 8U);
    }
    return static_cast<T>(value);
}

[[nodiscard]] bool checked_add(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left >
        std::numeric_limits<std::uint64_t>::max() -
            right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0 &&
        right >
            std::numeric_limits<std::uint64_t>::max() /
                left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] std::uint64_t input_size_u64(
    std::span<const std::byte> bytes) noexcept {
    if constexpr (
        sizeof(std::size_t) <= sizeof(std::uint64_t)) {
        return static_cast<std::uint64_t>(
            bytes.size());
    } else {
        const auto maximum =
            static_cast<std::size_t>(
                std::numeric_limits<
                    std::uint64_t>::max());
        return bytes.size() > maximum
                   ? std::numeric_limits<
                         std::uint64_t>::max()
                   : static_cast<std::uint64_t>(
                         bytes.size());
    }
}

[[nodiscard]] AgcShaderContainerError error(
    AgcShaderContainerErrorCode code,
    std::uint64_t file_offset,
    std::optional<std::size_t> section_index =
        std::nullopt) noexcept {
    return AgcShaderContainerError{
        .code = code,
        .section_index = section_index,
        .file_offset = file_offset,
    };
}

[[nodiscard]] SectionRecord read_section_record(
    std::span<const std::byte> bytes,
    std::uint64_t section_header_offset,
    std::size_t index) {
    const auto record_offset =
        static_cast<std::size_t>(
            section_header_offset +
            static_cast<std::uint64_t>(index) *
                kElf64SectionHeaderSize);

    return SectionRecord{
        .index = index,
        .name_index =
            read_little_endian<std::uint32_t>(
                bytes,
                record_offset),
        .type =
            read_little_endian<std::uint32_t>(
                bytes,
                record_offset + 4),
        .offset =
            read_little_endian<std::uint64_t>(
                bytes,
                record_offset + 24),
        .size =
            read_little_endian<std::uint64_t>(
                bytes,
                record_offset + 32),
        .alignment =
            read_little_endian<std::uint64_t>(
                bytes,
                record_offset + 48),
    };
}

[[nodiscard]] bool section_name_equals(
    std::span<const std::byte> bytes,
    std::uint64_t string_table_offset,
    std::uint64_t string_table_size,
    std::uint32_t name_index,
    std::string_view expected,
    bool& name_in_bounds,
    bool& terminated) noexcept {
    name_in_bounds =
        static_cast<std::uint64_t>(name_index) <
        string_table_size;
    terminated = false;
    if (!name_in_bounds) {
        return false;
    }

    const auto absolute =
        string_table_offset +
        static_cast<std::uint64_t>(name_index);
    const auto table_end =
        string_table_offset + string_table_size;
    const auto name_start =
        static_cast<std::size_t>(absolute);
    const auto name_end_limit =
        static_cast<std::size_t>(table_end);

    std::size_t cursor = name_start;
    while (cursor < name_end_limit) {
        if (bytes[cursor] == std::byte{0}) {
            terminated = true;
            break;
        }
        ++cursor;
    }
    if (!terminated) {
        return false;
    }

    const auto length = cursor - name_start;
    if (length != expected.size()) {
        return false;
    }

    for (std::size_t i = 0; i < length; ++i) {
        if (bytes[name_start + i] !=
            std::byte{
                static_cast<unsigned char>(
                    expected[i])}) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] AgcShaderProgramType program_type(
    std::uint8_t raw) noexcept {
    using Stage = AgcShaderStage;

    std::optional<Stage> known;
    switch (raw) {
    case 0:
        known = Stage::compute;
        break;
    case 1:
        known = Stage::pixel;
        break;
    case 2:
        known = Stage::geometry;
        break;
    case 3:
        known = Stage::hull;
        break;
    case 4:
        known = Stage::geometry_front;
        break;
    case 5:
        known = Stage::hull_front;
        break;
    case 6:
        known = Stage::geometry_back;
        break;
    case 7:
        known = Stage::hull_back;
        break;
    case 8:
        known = Stage::function;
        break;
    default:
        break;
    }

    return AgcShaderProgramType{
        .raw = raw,
        .known = known,
    };
}

}  // namespace

AgcShaderContainerResult
parse_agc_shader_container(
    std::span<const std::byte> bytes) {
    if (bytes.size() < kElfMagic.size()) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    file_too_small,
                0));
    }

    for (std::size_t i = 0;
         i < kElfMagic.size();
         ++i) {
        if (bytes[i] != kElfMagic[i]) {
            return AgcShaderContainerResult::failure(
                error(
                    AgcShaderContainerErrorCode::
                        bad_magic,
                    i));
        }
    }

    if (bytes.size() < 16) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    file_too_small,
                bytes.size()));
    }

    if (std::to_integer<std::uint8_t>(bytes[4]) !=
        kElfClass64) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    unsupported_class,
                4));
    }
    if (std::to_integer<std::uint8_t>(bytes[5]) !=
        kElfDataLittleEndian) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    unsupported_endianness,
                5));
    }
    if (std::to_integer<std::uint8_t>(bytes[6]) !=
        kElfCurrentVersion) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    unsupported_ident_version,
                6));
    }

    if (bytes.size() < kElf64HeaderSize) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    file_too_small,
                16));
    }

    const auto elf_type =
        read_little_endian<std::uint16_t>(
            bytes,
            16);
    const auto elf_version =
        read_little_endian<std::uint32_t>(
            bytes,
            20);
    const auto machine =
        read_little_endian<std::uint16_t>(
            bytes,
            18);
    const auto section_header_offset =
        read_little_endian<std::uint64_t>(
            bytes,
            40);
    const auto elf_flags =
        read_little_endian<std::uint32_t>(
            bytes,
            48);
    const auto elf_header_size =
        read_little_endian<std::uint16_t>(
            bytes,
            52);
    const auto section_header_entry_size =
        read_little_endian<std::uint16_t>(
            bytes,
            58);
    const auto section_header_count =
        read_little_endian<std::uint16_t>(
            bytes,
            60);
    const auto section_name_table_index =
        read_little_endian<std::uint16_t>(
            bytes,
            62);

    if (elf_version != kElfCurrentVersion) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    unsupported_elf_version,
                20));
    }
    if (machine != kEmAmdgpu) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    unsupported_machine,
                18));
    }
    if (elf_header_size != kElf64HeaderSize) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    invalid_elf_header_size,
                52));
    }
    if (section_header_offset == 0) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    missing_section_table,
                40));
    }
    if (section_header_count == 0) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    unsupported_extended_section_count,
                60));
    }
    if (section_header_entry_size !=
        kElf64SectionHeaderSize) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    invalid_section_header_entry_size,
                58));
    }
    if (section_name_table_index == 0 ||
        section_name_table_index == kShnXindex ||
        section_name_table_index >=
            section_header_count) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    invalid_section_name_table_index,
                62));
    }

    std::uint64_t section_table_size = 0;
    if (!checked_multiply(
            section_header_count,
            section_header_entry_size,
            section_table_size)) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    integer_overflow,
                60));
    }

    std::uint64_t section_table_end = 0;
    if (!checked_add(
            section_header_offset,
            section_table_size,
            section_table_end)) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    integer_overflow,
                40));
    }

    const auto input_size = input_size_u64(bytes);
    if (section_header_offset > input_size ||
        section_table_end > input_size) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    section_header_table_out_of_bounds,
                40));
    }

    const auto string_table =
        read_section_record(
            bytes,
            section_header_offset,
            section_name_table_index);
    if (string_table.type != kShtStrtab) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    invalid_section_name_table,
                string_table.offset,
                string_table.index));
    }

    std::uint64_t string_table_end = 0;
    if (!checked_add(
            string_table.offset,
            string_table.size,
            string_table_end)) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    integer_overflow,
                string_table.offset,
                string_table.index));
    }
    if (string_table.offset > input_size ||
        string_table_end > input_size) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    section_out_of_bounds,
                string_table.offset,
                string_table.index));
    }

    std::optional<SectionRecord> shader_header;
    std::optional<SectionRecord> shader_text;

    for (std::size_t index = 0;
         index < section_header_count;
         ++index) {
        const auto section =
            read_section_record(
                bytes,
                section_header_offset,
                index);

        if (section.type != kShtNobits &&
            section.size != 0) {
            std::uint64_t section_end = 0;
            if (!checked_add(
                    section.offset,
                    section.size,
                    section_end)) {
                return AgcShaderContainerResult::failure(
                    error(
                        AgcShaderContainerErrorCode::
                            integer_overflow,
                        section.offset,
                        section.index));
            }
            if (section.offset > input_size ||
                section_end > input_size) {
                return AgcShaderContainerResult::failure(
                    error(
                        AgcShaderContainerErrorCode::
                            section_out_of_bounds,
                        section.offset,
                        section.index));
            }
        }

        bool name_in_bounds = false;
        bool terminated = false;
        const auto is_header =
            section_name_equals(
                bytes,
                string_table.offset,
                string_table.size,
                section.name_index,
                ".shader_header",
                name_in_bounds,
                terminated);
        if (!name_in_bounds) {
            return AgcShaderContainerResult::failure(
                error(
                    AgcShaderContainerErrorCode::
                        section_name_out_of_bounds,
                    section_header_offset +
                        static_cast<std::uint64_t>(
                            index) *
                            kElf64SectionHeaderSize,
                    index));
        }
        if (!terminated) {
            return AgcShaderContainerResult::failure(
                error(
                    AgcShaderContainerErrorCode::
                        unterminated_section_name,
                    string_table.offset +
                        section.name_index,
                    index));
        }

        if (is_header) {
            if (section.type != kShtProgbits) {
                return AgcShaderContainerResult::failure(
                    error(
                        AgcShaderContainerErrorCode::
                            invalid_shader_section_type,
                        section_header_offset +
                            static_cast<std::uint64_t>(
                                index) *
                                kElf64SectionHeaderSize +
                            4U,
                        index));
            }
            if (shader_header.has_value()) {
                return AgcShaderContainerResult::failure(
                    error(
                        AgcShaderContainerErrorCode::
                            duplicate_shader_header,
                        section.offset,
                        index));
            }
            shader_header = section;
            continue;
        }

        name_in_bounds = false;
        terminated = false;
        const auto is_text =
            section_name_equals(
                bytes,
                string_table.offset,
                string_table.size,
                section.name_index,
                ".shader_text",
                name_in_bounds,
                terminated);
        if (!name_in_bounds) {
            return AgcShaderContainerResult::failure(
                error(
                    AgcShaderContainerErrorCode::
                        section_name_out_of_bounds,
                    section_header_offset +
                        static_cast<std::uint64_t>(
                            index) *
                            kElf64SectionHeaderSize,
                    index));
        }
        if (!terminated) {
            return AgcShaderContainerResult::failure(
                error(
                    AgcShaderContainerErrorCode::
                        unterminated_section_name,
                    string_table.offset +
                        section.name_index,
                    index));
        }

        if (is_text) {
            if (section.type != kShtProgbits) {
                return AgcShaderContainerResult::failure(
                    error(
                        AgcShaderContainerErrorCode::
                            invalid_shader_section_type,
                        section_header_offset +
                            static_cast<std::uint64_t>(
                                index) *
                                kElf64SectionHeaderSize +
                            4U,
                        index));
            }
            if (shader_text.has_value()) {
                return AgcShaderContainerResult::failure(
                    error(
                        AgcShaderContainerErrorCode::
                            duplicate_shader_text,
                        section.offset,
                        index));
            }
            shader_text = section;
        }
    }

    if (!shader_header.has_value()) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    missing_shader_header,
                0));
    }
    if (!shader_text.has_value()) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    missing_shader_text,
                0));
    }

    if (shader_header->size <
        kAgcHeaderMinimumSize) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    shader_header_too_small,
                shader_header->offset,
                shader_header->index));
    }

    const auto header_offset =
        static_cast<std::size_t>(
            shader_header->offset);
    const auto text_offset =
        static_cast<std::size_t>(
            shader_text->offset);
    const auto header_size =
        static_cast<std::size_t>(
            shader_header->size);
    const auto text_size =
        static_cast<std::size_t>(
            shader_text->size);

    const auto header_span =
        bytes.subspan(
            header_offset,
            header_size);
    const auto text_span =
        bytes.subspan(
            text_offset,
            text_size);

    const auto header_magic =
        read_little_endian<std::uint32_t>(
            header_span,
            0);
    if (header_magic != kAgcHeaderMagic) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    bad_shader_header_magic,
                shader_header->offset,
                shader_header->index));
    }

    const auto header_version =
        read_little_endian<std::uint32_t>(
            header_span,
            4);
    const auto declared_header_size =
        read_little_endian<std::uint32_t>(
            header_span,
            kAgcHeaderSizeOffset);
    const auto declared_shader_text_size =
        read_little_endian<std::uint32_t>(
            header_span,
            kAgcShaderTextSizeOffset);

    if (declared_header_size !=
        shader_header->size) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    declared_header_size_mismatch,
                shader_header->offset +
                    kAgcHeaderSizeOffset,
                shader_header->index));
    }
    if (declared_shader_text_size !=
        shader_text->size) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    declared_shader_text_size_mismatch,
                shader_header->offset +
                    kAgcShaderTextSizeOffset,
                shader_header->index));
    }

    if (text_size <
        kAgcShaderTextTrailerSize) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    shader_text_too_small_for_trailer,
                shader_text->offset,
                shader_text->index));
    }

    const auto trailer_offset =
        text_size -
        kAgcShaderTextTrailerSize;
    const auto program_byte_size =
        read_little_endian<std::uint32_t>(
            text_span,
            trailer_offset +
                kAgcTrailerProgramLengthOffset);
    const auto sl00_byte_size =
        read_little_endian<std::uint32_t>(
            text_span,
            trailer_offset +
                kAgcTrailerSl00LengthOffset);

    if (program_byte_size > trailer_offset) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    program_extent_out_of_bounds,
                shader_text->offset +
                    trailer_offset +
                    kAgcTrailerProgramLengthOffset,
                shader_text->index));
    }
    if ((program_byte_size % 4U) != 0U) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    program_size_not_dword_aligned,
                shader_text->offset +
                    trailer_offset +
                    kAgcTrailerProgramLengthOffset,
                shader_text->index));
    }

    try {
        std::vector<std::uint32_t> words;
        words.reserve(
            static_cast<std::size_t>(
                program_byte_size / 4U));
        for (std::size_t offset = 0;
             offset <
             static_cast<std::size_t>(
                 program_byte_size);
             offset += 4) {
            words.push_back(
                read_little_endian<
                    std::uint32_t>(
                    text_span,
                    offset));
        }

        return AgcShaderContainerResult::success(
            AgcShaderContainer{
                .os_abi =
                    std::to_integer<std::uint8_t>(
                        bytes[7]),
                .abi_version =
                    std::to_integer<std::uint8_t>(
                        bytes[8]),
                .elf_type = elf_type,
                .elf_machine = machine,
                .elf_flags = elf_flags,
                .header_magic = header_magic,
                .header_version = header_version,
                .declared_header_size =
                    declared_header_size,
                .declared_shader_text_size =
                    declared_shader_text_size,
                .program_type =
                    program_type(
                        std::to_integer<
                            std::uint8_t>(
                            header_span[
                                kAgcProgramTypeOffset])),
                .shader_header_section =
                    AgcShaderSectionProvenance{
                        .section_index =
                            shader_header->index,
                        .file_offset =
                            shader_header->offset,
                        .file_size =
                            shader_header->size,
                        .alignment =
                            shader_header->alignment,
                    },
                .shader_text_section =
                    AgcShaderSectionProvenance{
                        .section_index =
                            shader_text->index,
                        .file_offset =
                            shader_text->offset,
                        .file_size =
                            shader_text->size,
                        .alignment =
                            shader_text->alignment,
                    },
                .program_byte_size =
                    program_byte_size,
                .trailer_sl00_byte_size =
                    sl00_byte_size,
                .container_bytes =
                    std::vector<std::byte>(
                        bytes.begin(),
                        bytes.end()),
                .shader_header_bytes =
                    std::vector<std::byte>(
                        header_span.begin(),
                        header_span.end()),
                .shader_text_bytes =
                    std::vector<std::byte>(
                        text_span.begin(),
                        text_span.end()),
                .rdna2_words =
                    std::move(words),
            });
    } catch (const std::bad_alloc&) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    host_allocation_failure,
                0));
    } catch (const std::length_error&) {
        return AgcShaderContainerResult::failure(
            error(
                AgcShaderContainerErrorCode::
                    host_allocation_failure,
                0));
    }
}

}  // namespace astraea::graphics
