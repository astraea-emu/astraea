#include <astraea/graphics/agc_shader_binary.hpp>
#include <astraea/graphics/shader_ir.hpp>
#include <astraea/graphics/shader_program.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t kHeaderSize = 0x80;
constexpr std::size_t kTextSize = 0x60;
constexpr std::size_t kContextRegistersOffset = 0x60;
constexpr std::size_t kShaderRegistersOffset = 0x68;
constexpr std::size_t kTrailerOffset = kTextSize - 0x30;

template <typename T>
void write_little_endian(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    T value) {
    static_assert(std::is_unsigned_v<T>);

    for (std::size_t index = 0;
         index < sizeof(T);
         ++index) {
        bytes[offset + index] =
            std::byte{
                static_cast<unsigned char>(
                    (static_cast<std::uint64_t>(value) >>
                     (index * 8U)) &
                    0xffU)};
    }
}

struct RuntimeShaderFixture {
    std::vector<std::byte> header;
    std::vector<std::byte> text;
};

[[nodiscard]] RuntimeShaderFixture
make_runtime_shader_fixture() {
    RuntimeShaderFixture fixture{
        .header =
            std::vector<std::byte>(
                kHeaderSize,
                std::byte{0}),
        .text =
            std::vector<std::byte>(
                kTextSize,
                std::byte{0}),
    };

    write_little_endian<std::uint32_t>(
        fixture.header,
        0,
        0x34333231U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        4,
        0x18U);
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x18,
        kContextRegistersOffset - 0x18U);
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x20,
        kShaderRegistersOffset - 0x20U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        0x40,
        kHeaderSize);
    write_little_endian<std::uint32_t>(
        fixture.header,
        0x44,
        kTextSize);
    fixture.header[0x5a] = std::byte{1};
    fixture.header[0x5b] = std::byte{1};
    fixture.header[0x5c] = std::byte{1};

    write_little_endian<std::uint16_t>(
        fixture.header,
        kContextRegistersOffset,
        0x1234U);
    fixture.header[kContextRegistersOffset + 2] =
        std::byte{0xaa};
    fixture.header[kContextRegistersOffset + 3] =
        std::byte{0xbb};
    write_little_endian<std::uint32_t>(
        fixture.header,
        kContextRegistersOffset + 4,
        0x89abcdefU);

    write_little_endian<std::uint16_t>(
        fixture.header,
        kShaderRegistersOffset,
        0x000aU);
    fixture.header[kShaderRegistersOffset + 2] =
        std::byte{0xcc};
    fixture.header[kShaderRegistersOffset + 3] =
        std::byte{0xdd};
    write_little_endian<std::uint32_t>(
        fixture.header,
        kShaderRegistersOffset + 4,
        0x10203040U);

    write_little_endian<std::uint32_t>(
        fixture.text,
        0,
        0xbf800000U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        4,
        0xbf810000U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        kTrailerOffset + 0x14,
        8U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        kTrailerOffset + 0x1c,
        4U);

    return fixture;
}

[[nodiscard]]
astraea::graphics::AgcShaderBinaryError
parse_error(
    const RuntimeShaderFixture& fixture) {
    const auto result =
        astraea::graphics::
            parse_agc_shader_binary(
                fixture.header,
                fixture.text);
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

}  // namespace

TEST_CASE(
    "runtime AGC shader pair exposes evidenced register lists and RDNA2 program",
    "[graphics][agc][shader-binary]") {
    const auto fixture =
        make_runtime_shader_fixture();

    const auto result =
        astraea::graphics::
            parse_agc_shader_binary(
                fixture.header,
                fixture.text);

    REQUIRE(result.has_value());
    REQUIRE(result->header_magic == 0x34333231U);
    REQUIRE(result->header_version == 0x18U);
    REQUIRE(result->declared_header_size == kHeaderSize);
    REQUIRE(
        result->declared_shader_text_size ==
        kTextSize);
    REQUIRE(result->program_type.raw == 1);
    REQUIRE(
        result->program_type.known ==
        std::optional<
            astraea::graphics::AgcShaderStage>{
            astraea::graphics::AgcShaderStage::
                pixel});

    REQUIRE(
        result->context_register_list_header_offset ==
        std::optional<std::uint64_t>{
            kContextRegistersOffset});
    REQUIRE(
        result->shader_register_list_header_offset ==
        std::optional<std::uint64_t>{
            kShaderRegistersOffset});
    REQUIRE(result->context_registers.size() == 1);
    REQUIRE(
        result->context_registers[0] ==
        astraea::graphics::AgcRegisterWrite{
            .register_offset = 0x1234U,
            .value = 0x89abcdefU,
        });

    REQUIRE(result->shader_registers.size() == 1);
    REQUIRE(
        result->shader_registers[0] ==
        astraea::graphics::AgcRegisterWrite{
            .register_offset = 0x000aU,
            .value = 0x10203040U,
        });

    REQUIRE(result->program_byte_size == 8);
    REQUIRE(result->trailer_sl00_byte_size == 4);
    REQUIRE(result->rdna2_words.size() == 2);
    REQUIRE(result->rdna2_words[0] == 0xbf800000U);
    REQUIRE(result->rdna2_words[1] == 0xbf810000U);
    REQUIRE(result->shader_header_bytes == fixture.header);
    REQUIRE(result->shader_text_bytes == fixture.text);
}


TEST_CASE(
    "runtime AGC register-list deltas are self-relative to their pointer fields",
    "[graphics][agc][shader-binary][registers][self-relative]") {
    auto fixture =
        make_runtime_shader_fixture();

    constexpr std::size_t kObservedShapeHeaderSize = 0x160;
    constexpr std::size_t kContextField = 0x18;
    constexpr std::size_t kShaderField = 0x20;
    constexpr std::uint64_t kContextDelta = 0xb0;
    constexpr std::uint64_t kShaderDelta = 0x78;
    constexpr std::size_t kContextTarget =
        kContextField + kContextDelta;
    constexpr std::size_t kShaderTarget =
        kShaderField + kShaderDelta;

    fixture.header.resize(
        kObservedShapeHeaderSize,
        std::byte{0});
    write_little_endian<std::uint32_t>(
        fixture.header,
        0x40,
        static_cast<std::uint32_t>(
            fixture.header.size()));
    write_little_endian<std::uint64_t>(
        fixture.header,
        kContextField,
        kContextDelta);
    write_little_endian<std::uint64_t>(
        fixture.header,
        kShaderField,
        kShaderDelta);
    fixture.header[0x5b] = std::byte{1};
    fixture.header[0x5c] = std::byte{1};

    // Decoys at the incorrect header-absolute interpretation.
    write_little_endian<std::uint16_t>(
        fixture.header,
        static_cast<std::size_t>(kContextDelta),
        0xdeadU);
    write_little_endian<std::uint32_t>(
        fixture.header,
        static_cast<std::size_t>(kContextDelta) + 4U,
        0xaaaaaaaaU);
    write_little_endian<std::uint16_t>(
        fixture.header,
        static_cast<std::size_t>(kShaderDelta),
        0xbeefU);
    write_little_endian<std::uint32_t>(
        fixture.header,
        static_cast<std::size_t>(kShaderDelta) + 4U,
        0xbbbbbbbbU);

    // Coherent records at field + raw_delta.
    write_little_endian<std::uint16_t>(
        fixture.header,
        kContextTarget,
        0x01c4U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        kContextTarget + 4U,
        0x00000004U);
    write_little_endian<std::uint16_t>(
        fixture.header,
        kShaderTarget,
        0x0008U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        kShaderTarget + 4U,
        0x12345678U);

    const auto result =
        astraea::graphics::
            parse_agc_shader_binary(
                fixture.header,
                fixture.text);

    REQUIRE(result.has_value());
    REQUIRE(result->context_registers.size() == 1);
    REQUIRE(
        result->context_registers[0] ==
        astraea::graphics::AgcRegisterWrite{
            .register_offset = 0x01c4U,
            .value = 0x00000004U,
        });
    REQUIRE(result->shader_registers.size() == 1);
    REQUIRE(
        result->shader_registers[0] ==
        astraea::graphics::AgcRegisterWrite{
            .register_offset = 0x0008U,
            .value = 0x12345678U,
        });
}

TEST_CASE(
    "zero-count AGC register lists do not assign pointer semantics to raw offsets",
    "[graphics][agc][shader-binary][registers]") {
    auto fixture =
        make_runtime_shader_fixture();
    fixture.header[0x5b] = std::byte{0};
    fixture.header[0x5c] = std::byte{0};
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x18,
        std::numeric_limits<std::uint64_t>::max());
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x20,
        std::numeric_limits<std::uint64_t>::max());

    const auto result =
        astraea::graphics::
            parse_agc_shader_binary(
                fixture.header,
                fixture.text);

    REQUIRE(result.has_value());
    REQUIRE_FALSE(
        result->context_register_list_header_offset.has_value());
    REQUIRE_FALSE(
        result->shader_register_list_header_offset.has_value());
    REQUIRE(result->context_registers.empty());
    REQUIRE(result->shader_registers.empty());
}

TEST_CASE(
    "runtime AGC register table extents fail deterministically",
    "[graphics][agc][shader-binary][registers][malformed]") {
    SECTION("context register extent overflows") {
        auto fixture =
            make_runtime_shader_fixture();
        write_little_endian<std::uint64_t>(
            fixture.header,
            0x18,
            std::numeric_limits<std::uint64_t>::max() -
                3U);

        const auto error = parse_error(fixture);
        REQUIRE(
            error.code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    register_table_extent_overflow);
        REQUIRE(
            error.region ==
            astraea::graphics::
                AgcShaderBinaryRegion::
                    shader_header);
        REQUIRE(
            error.register_list_kind ==
            std::optional<
                astraea::graphics::
                    AgcRegisterListKind>{
                astraea::graphics::
                    AgcRegisterListKind::context});
    }

    SECTION("shader register extent is outside header") {
        auto fixture =
            make_runtime_shader_fixture();
        write_little_endian<std::uint64_t>(
            fixture.header,
            0x20,
            kHeaderSize - 4U);

        const auto error = parse_error(fixture);
        REQUIRE(
            error.code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    register_table_extent_out_of_bounds);
        REQUIRE(
            error.register_list_kind ==
            std::optional<
                astraea::graphics::
                    AgcRegisterListKind>{
                astraea::graphics::
                    AgcRegisterListKind::shader});
    }
}

TEST_CASE(
    "runtime AGC shader pair validates evidenced structural invariants",
    "[graphics][agc][shader-binary][malformed]") {
    SECTION("magic") {
        auto fixture =
            make_runtime_shader_fixture();
        write_little_endian<std::uint32_t>(
            fixture.header,
            0,
            0x12345678U);
        REQUIRE(
            parse_error(fixture).code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    bad_shader_header_magic);
    }

    SECTION("declared header size") {
        auto fixture =
            make_runtime_shader_fixture();
        write_little_endian<std::uint32_t>(
            fixture.header,
            0x40,
            kHeaderSize - 1U);
        REQUIRE(
            parse_error(fixture).code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    declared_header_size_mismatch);
    }

    SECTION("declared shader text size") {
        auto fixture =
            make_runtime_shader_fixture();
        write_little_endian<std::uint32_t>(
            fixture.header,
            0x44,
            kTextSize - 1U);
        REQUIRE(
            parse_error(fixture).code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    declared_shader_text_size_mismatch);
    }

    SECTION("shader text is too short for trailer") {
        auto fixture =
            make_runtime_shader_fixture();
        fixture.text.resize(0x20);
        write_little_endian<std::uint32_t>(
            fixture.header,
            0x44,
            static_cast<std::uint32_t>(
                fixture.text.size()));
        REQUIRE(
            parse_error(fixture).code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    shader_text_too_small_for_trailer);
    }

    SECTION("program overlaps trailer") {
        auto fixture =
            make_runtime_shader_fixture();
        write_little_endian<std::uint32_t>(
            fixture.text,
            kTrailerOffset + 0x14,
            static_cast<std::uint32_t>(
                kTrailerOffset + 4U));
        REQUIRE(
            parse_error(fixture).code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    program_extent_out_of_bounds);
    }

    SECTION("program length is not dword aligned") {
        auto fixture =
            make_runtime_shader_fixture();
        write_little_endian<std::uint32_t>(
            fixture.text,
            kTrailerOffset + 0x14,
            6U);
        REQUIRE(
            parse_error(fixture).code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    program_size_not_dword_aligned);
    }
}

TEST_CASE(
    "runtime AGC shader pair feeds the existing Shader IR pipeline",
    "[graphics][agc][shader-binary][shader-ir]") {
    const auto fixture =
        make_runtime_shader_fixture();
    const auto shader =
        astraea::graphics::
            parse_agc_shader_binary(
                fixture.header,
                fixture.text);
    REQUIRE(shader.has_value());

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(
                shader->rdna2_words);

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
