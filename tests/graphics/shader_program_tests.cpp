#include <astraea/graphics/shader_program.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kSoppBase = 0xbf800000U;
constexpr std::uint32_t kSop1Base = 0xbe800000U;
constexpr std::uint32_t kVop1Base = 0x7e000000U;

constexpr std::uint32_t make_sopp(
    std::uint8_t opcode,
    std::uint16_t simm16) {
    return kSoppBase |
           (static_cast<std::uint32_t>(opcode) << 16U) |
           static_cast<std::uint32_t>(simm16);
}

constexpr std::uint32_t make_sop1(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint8_t source) {
    return kSop1Base |
           (static_cast<std::uint32_t>(destination) << 16U) |
           (static_cast<std::uint32_t>(opcode) << 8U) |
           static_cast<std::uint32_t>(source);
}

constexpr std::uint32_t make_vop1(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source) {
    return kVop1Base |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(opcode) << 9U) |
           static_cast<std::uint32_t>(source);
}

constexpr std::uint32_t make_vop2(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source0,
    std::uint8_t source1) {
    return (static_cast<std::uint32_t>(opcode) << 25U) |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(source1) << 9U) |
           static_cast<std::uint32_t>(source0);
}

}  // namespace

TEST_CASE(
    "empty RDNA2 stream lowers to empty Shader IR program",
    "[graphics][shader-program]") {
    const std::array<std::uint32_t, 0> words{};

    const auto result =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);

    REQUIRE(result.has_value());
    REQUIRE(result->source_word_count == 0);
    REQUIRE(result->emissions.empty());
}

TEST_CASE(
    "bounded RDNA2 stream preserves variable instruction extents and order",
    "[graphics][shader-program]") {
    const std::array<std::uint32_t, 5> words{
        make_sop1(3, 5, 255),
        0x3f800000U,
        make_vop1(1, 1, 258),
        make_vop2(3, 3, 257, 2),
        make_sopp(1, 0),
    };

    const auto result =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);

    REQUIRE(result.has_value());
    REQUIRE(result->source_word_count == 5);
    REQUIRE(result->emissions.size() == 4);

    const auto& literal_move =
        result->emissions[0];
    REQUIRE(
        literal_move.provenance.source_instruction.word_index ==
        0);
    REQUIRE(
        literal_move.provenance.source_instruction.byte_offset ==
        0);
    REQUIRE(
        literal_move.provenance.source_instruction.word_count ==
        2);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrScalarMove32>(
            literal_move.operation));
    const auto& scalar_move =
        std::get<
            astraea::graphics::ShaderIrScalarMove32>(
            literal_move.operation);
    REQUIRE(scalar_move.destination.index == 5);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrLiteral32>(
            scalar_move.source));
    REQUIRE(
        std::get<
            astraea::graphics::ShaderIrLiteral32>(
            scalar_move.source)
            .bits == 0x3f800000U);

    const auto& vector_move =
        result->emissions[1];
    REQUIRE(
        vector_move.provenance.source_instruction.word_index ==
        2);
    REQUIRE(
        vector_move.provenance.source_instruction.byte_offset ==
        8);
    REQUIRE(
        vector_move.provenance.source_instruction.word_count ==
        1);
    const auto& move =
        std::get<
            astraea::graphics::ShaderIrVectorMove32>(
            vector_move.operation);
    REQUIRE(move.destination.index == 1);
    REQUIRE(move.source.index == 2);

    const auto& vector_add =
        result->emissions[2];
    REQUIRE(
        vector_add.provenance.source_instruction.word_index ==
        3);
    REQUIRE(
        vector_add.provenance.source_instruction.byte_offset ==
        12);
    REQUIRE(
        vector_add.provenance.source_instruction.word_count ==
        1);
    const auto& add =
        std::get<
            astraea::graphics::ShaderIrVectorAddF32>(
            vector_add.operation);
    REQUIRE(add.destination.index == 3);
    REQUIRE(add.source0.index == 1);
    REQUIRE(add.source1.index == 2);

    const auto& end =
        result->emissions[3];
    REQUIRE(
        end.provenance.source_instruction.word_index == 4);
    REQUIRE(
        end.provenance.source_instruction.byte_offset == 16);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrEndProgram>(
            end.operation));
}

TEST_CASE(
    "extension dword is never decoded as a standalone instruction",
    "[graphics][shader-program]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 7, 255),
        0xbf810000U,
        make_sopp(1, 0),
    };

    const auto result =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);

    REQUIRE(result.has_value());
    REQUIRE(result->emissions.size() == 2);
    REQUIRE(
        result->emissions[0]
            .provenance.source_instruction.word_count == 2);
    REQUIRE(
        result->emissions[1]
            .provenance.source_instruction.word_index == 2);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrEndProgram>(
            result->emissions[1].operation));
}

TEST_CASE(
    "mid-stream missing extension reports failing instruction and progress",
    "[graphics][shader-program]") {
    const std::array<std::uint32_t, 2> words{
        make_sopp(0, 0),
        make_vop1(1, 5, 255),
    };

    const auto result =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::ShaderIrProgramErrorCode::
            decode_failure);
    REQUIRE(result.error().word_index == 1);
    REQUIRE(result.error().lowered_instruction_count == 1);
    REQUIRE(result.error().decode_error.has_value());
    REQUIRE(
        result.error().decode_error->code ==
        astraea::graphics::Rdna2DecodeErrorCode::
            instruction_out_of_bounds);
    REQUIRE(result.error().decode_error->word_index == 2);
    REQUIRE(result.error().decode_error->available_words == 2);
}

TEST_CASE(
    "Shader IR program lowers branches linearly without following control flow",
    "[graphics][shader-program]") {
    const std::array<std::uint32_t, 2> words{
        make_sopp(2, 4),
        make_sopp(1, 0),
    };

    const auto result =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);

    REQUIRE(result.has_value());
    REQUIRE(result->emissions.size() == 2);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrRelativeBranch>(
            result->emissions[0].operation));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrEndProgram>(
            result->emissions[1].operation));
    REQUIRE(
        result->emissions[1]
            .provenance.source_instruction.word_index == 1);
}
