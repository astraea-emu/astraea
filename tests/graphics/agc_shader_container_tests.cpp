#include <astraea/graphics/agc_shader_container.hpp>
#include <astraea/graphics/shader_program.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t kHeaderOffset = 0x40;
constexpr std::size_t kHeaderSize = 0x60;
constexpr std::size_t kTextOffset = 0xa0;
constexpr std::size_t kTextSize = 0x50;
constexpr std::size_t kStringTableOffset = 0xf0;
constexpr std::size_t kStringTableSize = 39;
constexpr std::size_t kSectionTableOffset = 0x118;
constexpr std::size_t kSectionHeaderSize = 64;
constexpr std::size_t kSectionCount = 4;

constexpr std::uint32_t kShaderHeaderNameIndex = 1;
constexpr std::uint32_t kShaderTextNameIndex = 16;
constexpr std::uint32_t kStringTableNameIndex = 29;

template <typename T>
void write_little_endian(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    T value) {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[offset + i] =
            std::byte{
                static_cast<unsigned char>(
                    (static_cast<std::uint64_t>(value) >>
                     (i * 8U)) &
                    0xffU)};
    }
}

void write_ascii(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::span<const char> text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[offset + i] =
            std::byte{
                static_cast<unsigned char>(
                    text[i])};
    }
}

void write_section(
    std::vector<std::byte>& bytes,
    std::size_t index,
    std::uint32_t name_index,
    std::uint32_t type,
    std::uint64_t offset,
    std::uint64_t size,
    std::uint64_t alignment) {
    const auto record =
        kSectionTableOffset +
        index * kSectionHeaderSize;
    write_little_endian<std::uint32_t>(
        bytes,
        record,
        name_index);
    write_little_endian<std::uint32_t>(
        bytes,
        record + 4,
        type);
    write_little_endian<std::uint64_t>(
        bytes,
        record + 24,
        offset);
    write_little_endian<std::uint64_t>(
        bytes,
        record + 32,
        size);
    write_little_endian<std::uint64_t>(
        bytes,
        record + 48,
        alignment);
}

[[nodiscard]] std::vector<std::byte>
make_synthetic_agc_shader(
    std::uint8_t program_type = 1) {
    std::vector<std::byte> bytes(
        kSectionTableOffset +
            kSectionCount * kSectionHeaderSize,
        std::byte{0});

    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{2};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    bytes[7] = std::byte{0};
    bytes[8] = std::byte{0};

    write_little_endian<std::uint16_t>(
        bytes,
        16,
        2);
    write_little_endian<std::uint16_t>(
        bytes,
        18,
        224);
    write_little_endian<std::uint32_t>(
        bytes,
        20,
        1);
    write_little_endian<std::uint64_t>(
        bytes,
        40,
        kSectionTableOffset);
    write_little_endian<std::uint16_t>(
        bytes,
        52,
        64);
    write_little_endian<std::uint16_t>(
        bytes,
        58,
        kSectionHeaderSize);
    write_little_endian<std::uint16_t>(
        bytes,
        60,
        kSectionCount);
    write_little_endian<std::uint16_t>(
        bytes,
        62,
        3);

    write_little_endian<std::uint32_t>(
        bytes,
        kHeaderOffset,
        0x34333231U);
    write_little_endian<std::uint32_t>(
        bytes,
        kHeaderOffset + 4,
        7);
    write_little_endian<std::uint32_t>(
        bytes,
        kHeaderOffset + 0x40,
        kHeaderSize);
    write_little_endian<std::uint32_t>(
        bytes,
        kHeaderOffset + 0x44,
        kTextSize);
    bytes[kHeaderOffset + 0x5a] =
        std::byte{program_type};

    // Two already-supported RDNA2 SOPP instructions:
    // S_NOP 0 and S_ENDPGM.
    write_little_endian<std::uint32_t>(
        bytes,
        kTextOffset,
        0xbf800000U);
    write_little_endian<std::uint32_t>(
        bytes,
        kTextOffset + 4,
        0xbf810000U);

    constexpr std::array<char, 4> sl00{
        's',
        'l',
        '0',
        '0',
    };
    write_ascii(
        bytes,
        kTextOffset + 0x10,
        sl00);

    constexpr auto trailer_offset =
        kTextOffset + kTextSize - 0x30;
    write_little_endian<std::uint32_t>(
        bytes,
        trailer_offset + 0x14,
        8);
    write_little_endian<std::uint32_t>(
        bytes,
        trailer_offset + 0x1c,
        4);

    constexpr std::array<char, kStringTableSize>
        names{
            '\0',
            '.',
            's',
            'h',
            'a',
            'd',
            'e',
            'r',
            '_',
            'h',
            'e',
            'a',
            'd',
            'e',
            'r',
            '\0',
            '.',
            's',
            'h',
            'a',
            'd',
            'e',
            'r',
            '_',
            't',
            'e',
            'x',
            't',
            '\0',
            '.',
            's',
            'h',
            's',
            't',
            'r',
            't',
            'a',
            'b',
            '\0',
        };
    write_ascii(
        bytes,
        kStringTableOffset,
        names);

    write_section(
        bytes,
        1,
        kShaderHeaderNameIndex,
        1,
        kHeaderOffset,
        kHeaderSize,
        8);
    write_section(
        bytes,
        2,
        kShaderTextNameIndex,
        1,
        kTextOffset,
        kTextSize,
        16);
    write_section(
        bytes,
        3,
        kStringTableNameIndex,
        3,
        kStringTableOffset,
        kStringTableSize,
        1);

    return bytes;
}

[[nodiscard]] astraea::graphics::AgcShaderContainerErrorCode
parse_error(const std::vector<std::byte>& bytes) {
    const auto result =
        astraea::graphics::
            parse_agc_shader_container(bytes);
    REQUIRE_FALSE(result.has_value());
    return result.error().code;
}

}  // namespace

TEST_CASE(
    "AGC shader container exposes evidence-bounded RDNA2 program and provenance",
    "[graphics][agc][shader-container]") {
    const auto bytes =
        make_synthetic_agc_shader();

    const auto result =
        astraea::graphics::
            parse_agc_shader_container(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->elf_type == 2);
    REQUIRE(result->elf_machine == 224);
    REQUIRE(result->shader.header_magic == 0x34333231U);
    REQUIRE(result->shader.header_version == 7);
    REQUIRE(result->shader.declared_header_size == kHeaderSize);
    REQUIRE(
        result->shader.declared_shader_text_size ==
        kTextSize);
    REQUIRE(result->shader.program_type.raw == 1);
    REQUIRE(
        result->shader.program_type.known ==
        std::optional<
            astraea::graphics::AgcShaderStage>{
            astraea::graphics::AgcShaderStage::
                pixel});
    REQUIRE(result->shader.program_byte_size == 8);
    REQUIRE(result->shader.trailer_sl00_byte_size == 4);
    REQUIRE(result->shader.rdna2_words.size() == 2);
    REQUIRE(result->shader.rdna2_words[0] == 0xbf800000U);
    REQUIRE(result->shader.rdna2_words[1] == 0xbf810000U);
    REQUIRE(
        result->shader_header_section.section_index ==
        1);
    REQUIRE(
        result->shader_header_section.file_offset ==
        kHeaderOffset);
    REQUIRE(
        result->shader_text_section.section_index ==
        2);
    REQUIRE(
        result->shader_text_section.file_offset ==
        kTextOffset);
    REQUIRE(
        result->container_bytes ==
        bytes);
    REQUIRE(
        result->shader.shader_header_bytes.size() ==
        kHeaderSize);
    REQUIRE(
        result->shader.shader_text_bytes.size() ==
        kTextSize);
}

TEST_CASE(
    "AGC container delegates raw header and text semantics to the canonical runtime parser",
    "[graphics][agc][shader-container][shader-binary]") {
    const auto container =
        astraea::graphics::
            parse_agc_shader_container(
                make_synthetic_agc_shader());
    REQUIRE(container.has_value());

    const auto direct =
        astraea::graphics::
            parse_agc_shader_binary(
                container->shader.shader_header_bytes,
                container->shader.shader_text_bytes);
    REQUIRE(direct.has_value());
    REQUIRE(direct.value() == container->shader);
}

TEST_CASE(
    "unknown AGC program type stays raw instead of being guessed",
    "[graphics][agc][shader-container][program-type]") {
    const auto bytes =
        make_synthetic_agc_shader(0x7f);

    const auto result =
        astraea::graphics::
            parse_agc_shader_container(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->shader.program_type.raw == 0x7f);
    REQUIRE_FALSE(
        result->shader.program_type.known.has_value());
}

TEST_CASE(
    "AGC shader container feeds existing RDNA2 to Shader IR pipeline end to end",
    "[graphics][agc][shader-container][shader-ir]") {
    const auto container =
        astraea::graphics::
            parse_agc_shader_container(
                make_synthetic_agc_shader());
    REQUIRE(container.has_value());

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(
                container->shader.rdna2_words);

    REQUIRE(program.has_value());
    REQUIRE(program->source_word_count == 2);
    REQUIRE(program->emissions.size() == 2);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrNop>(
            program->emissions[0].operation));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrEndProgram>(
            program->emissions[1].operation));
}

TEST_CASE(
    "AGC shader container rejects malformed ELF envelope before AGC semantics",
    "[graphics][agc][shader-container][malformed]") {
    SECTION("wrong ELF version") {
        auto bytes =
            make_synthetic_agc_shader();
        write_little_endian<std::uint32_t>(
            bytes,
            20,
            2);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    unsupported_elf_version);
    }

    SECTION("wrong machine") {
        auto bytes =
            make_synthetic_agc_shader();
        write_little_endian<std::uint16_t>(
            bytes,
            18,
            62);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    unsupported_machine);
    }

    SECTION("section table arithmetic overflow") {
        auto bytes =
            make_synthetic_agc_shader();
        write_little_endian<std::uint64_t>(
            bytes,
            40,
            std::numeric_limits<
                std::uint64_t>::max() -
                31U);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    integer_overflow);
    }

    SECTION("section payload out of bounds") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto text_record =
            kSectionTableOffset +
            2 * kSectionHeaderSize;
        write_little_endian<std::uint64_t>(
            bytes,
            text_record + 24,
            bytes.size() - 4U);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    section_out_of_bounds);
    }

    SECTION("section name index out of bounds") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto header_record =
            kSectionTableOffset +
            kSectionHeaderSize;
        write_little_endian<std::uint32_t>(
            bytes,
            header_record,
            kStringTableSize);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    section_name_out_of_bounds);
    }

    SECTION("unterminated section name") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto string_record =
            kSectionTableOffset +
            3 * kSectionHeaderSize;
        write_little_endian<std::uint64_t>(
            bytes,
            string_record + 32,
            15);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    unterminated_section_name);
    }
}

TEST_CASE(
    "AGC shader container requires exactly one file-backed header and text section",
    "[graphics][agc][shader-container][sections]") {
    SECTION("named shader section is not PROGBITS") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto text_record =
            kSectionTableOffset +
            2 * kSectionHeaderSize;
        write_little_endian<std::uint32_t>(
            bytes,
            text_record + 4,
            8);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    invalid_shader_section_type);
    }

    SECTION("missing header") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto header_record =
            kSectionTableOffset +
            kSectionHeaderSize;
        write_little_endian<std::uint32_t>(
            bytes,
            header_record,
            kStringTableNameIndex);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    missing_shader_header);
    }

    SECTION("duplicate text") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto header_record =
            kSectionTableOffset +
            kSectionHeaderSize;
        write_little_endian<std::uint32_t>(
            bytes,
            header_record,
            kShaderTextNameIndex);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    duplicate_shader_text);
    }
}

TEST_CASE(
    "AGC shader container validates evidence-backed header invariants",
    "[graphics][agc][shader-container][header]") {
    SECTION("header is too short") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto header_record =
            kSectionTableOffset +
            kSectionHeaderSize;
        write_little_endian<std::uint64_t>(
            bytes,
            header_record + 32,
            0x50);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    shader_header_too_small);
    }

    SECTION("header magic") {
        auto bytes =
            make_synthetic_agc_shader();
        write_little_endian<std::uint32_t>(
            bytes,
            kHeaderOffset,
            0x12345678U);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    bad_shader_header_magic);
    }

    SECTION("declared header size") {
        auto bytes =
            make_synthetic_agc_shader();
        write_little_endian<std::uint32_t>(
            bytes,
            kHeaderOffset + 0x40,
            kHeaderSize + 8U);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    declared_header_size_mismatch);
    }

    SECTION("declared shader-text size") {
        auto bytes =
            make_synthetic_agc_shader();
        write_little_endian<std::uint32_t>(
            bytes,
            kHeaderOffset + 0x44,
            kTextSize + 4U);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    declared_shader_text_size_mismatch);
    }
}

TEST_CASE(
    "AGC shader container bounds the documented program prefix",
    "[graphics][agc][shader-container][program-extent]") {
    SECTION("shader text cannot hold trailer") {
        auto bytes =
            make_synthetic_agc_shader();
        const auto text_record =
            kSectionTableOffset +
            2 * kSectionHeaderSize;
        write_little_endian<std::uint64_t>(
            bytes,
            text_record + 32,
            40);
        write_little_endian<std::uint32_t>(
            bytes,
            kHeaderOffset + 0x44,
            40);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    shader_text_too_small_for_trailer);
    }

    SECTION("program overlaps trailer") {
        auto bytes =
            make_synthetic_agc_shader();
        constexpr auto trailer_offset =
            kTextOffset + kTextSize - 0x30;
        write_little_endian<std::uint32_t>(
            bytes,
            trailer_offset + 0x14,
            36);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    program_extent_out_of_bounds);
    }

    SECTION("program is not whole dwords") {
        auto bytes =
            make_synthetic_agc_shader();
        constexpr auto trailer_offset =
            kTextOffset + kTextSize - 0x30;
        write_little_endian<std::uint32_t>(
            bytes,
            trailer_offset + 0x14,
            6);
        REQUIRE(
            parse_error(bytes) ==
            astraea::graphics::
                AgcShaderContainerErrorCode::
                    program_size_not_dword_aligned);
    }
}
