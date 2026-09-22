#include <astraea/graphics/rdna2_decoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kSoppBase = 0xbf800000U;

constexpr std::uint32_t make_sopp(
    std::uint8_t opcode,
    std::uint16_t simm16) {
    return kSoppBase |
           (static_cast<std::uint32_t>(opcode) << 16U) |
           static_cast<std::uint32_t>(simm16);
}

}  // namespace

TEST_CASE(
    "RDNA2 SOPP S_NOP preserves encoding and immediate",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(0, 0x000f),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(result->word_index == 0);
    REQUIRE(result->byte_offset == 0);
    REQUIRE(result->raw_word == 0xbf80000fU);
    REQUIRE(
        result->format ==
        astraea::graphics::Rdna2InstructionFormat::sopp);
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::s_nop);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 0);
    REQUIRE(result->sopp->simm16 == 15);

    const std::array<std::byte, 4> expected_bytes{
        std::byte{0x0f},
        std::byte{0x00},
        std::byte{0x80},
        std::byte{0xbf},
    };
    REQUIRE(result->raw_encoding == expected_bytes);
}

TEST_CASE(
    "RDNA2 SOPP S_ENDPGM is classified",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(1, 0),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::s_endpgm);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 1);
    REQUIRE(result->sopp->simm16 == 0);
}

TEST_CASE(
    "RDNA2 SOPP S_BRANCH sign-extends SIMM16",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(2, 0xfffe),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::s_branch);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 2);
    REQUIRE(result->sopp->simm16 == -2);
}

TEST_CASE(
    "unknown RDNA2 SOPP opcode remains typed and observable",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(0x7e, 0x1234),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->format ==
        astraea::graphics::Rdna2InstructionFormat::sopp);
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::
            unknown_sopp_opcode);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 0x7e);
    REQUIRE(result->sopp->simm16 == 0x1234);
    REQUIRE(result->raw_word == words[0]);
}

TEST_CASE(
    "unsupported RDNA2 encoding preserves the raw instruction",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 1> words{
        0x01234567U,
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->format ==
        astraea::graphics::Rdna2InstructionFormat::
            unsupported);
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::
            unsupported_encoding);
    REQUIRE_FALSE(result->sopp.has_value());
    REQUIRE(result->raw_word == words[0]);
}

TEST_CASE(
    "RDNA2 decoder records deterministic source offsets",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 2> words{
        0x01234567U,
        make_sopp(1, 0),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            1);

    REQUIRE(result.has_value());
    REQUIRE(result->word_index == 1);
    REQUIRE(result->byte_offset == 4);
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::s_endpgm);
}

TEST_CASE(
    "RDNA2 decoder rejects an out-of-bounds instruction fetch",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(0, 0),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            1);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::Rdna2DecodeErrorCode::
            instruction_out_of_bounds);
    REQUIRE(result.error().word_index == 1);
    REQUIRE(result.error().available_words == 1);
}

TEST_CASE(
    "RDNA2 decoder rejects source-offset multiplication overflow",
    "[graphics][rdna2]") {
    const std::array<std::uint32_t, 0> words{};

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            std::numeric_limits<std::size_t>::max());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::Rdna2DecodeErrorCode::
            source_offset_overflow);
    REQUIRE(
        result.error().word_index ==
        std::numeric_limits<std::size_t>::max());
}


TEST_CASE(
    "RDNA2 SOPP conditional branch opcodes are classified exactly",
    "[graphics][rdna2][conditional-branch]") {
    struct Case {
        std::uint8_t opcode;
        astraea::graphics::Rdna2InstructionKind kind;
    };

    constexpr std::array<Case, 6> cases{{
        {4, astraea::graphics::Rdna2InstructionKind::s_cbranch_scc0},
        {5, astraea::graphics::Rdna2InstructionKind::s_cbranch_scc1},
        {6, astraea::graphics::Rdna2InstructionKind::s_cbranch_vccz},
        {7, astraea::graphics::Rdna2InstructionKind::s_cbranch_vccnz},
        {8, astraea::graphics::Rdna2InstructionKind::s_cbranch_execz},
        {9, astraea::graphics::Rdna2InstructionKind::s_cbranch_execnz},
    }};

    for (const auto& test_case : cases) {
        const std::array<std::uint32_t, 1> words{
            make_sopp(test_case.opcode, 0xfffe),
        };

        const auto result =
            astraea::graphics::decode_rdna2_instruction(
                words,
                0);

        REQUIRE(result.has_value());
        REQUIRE(result->kind == test_case.kind);
        REQUIRE(result->format ==
                astraea::graphics::Rdna2InstructionFormat::sopp);
        REQUIRE(result->sopp.has_value());
        REQUIRE(result->sopp->opcode == test_case.opcode);
        REQUIRE(result->sopp->simm16 == -2);
        REQUIRE(result->raw_word == words[0]);
    }
}

TEST_CASE(
    "S_WAKEUP remains explicit unknown until wave semantics are scoped",
    "[graphics][rdna2][conditional-branch]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(3, 0),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::
            unknown_sopp_opcode);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 3);
}


TEST_CASE(
    "RDNA2 SOPP S_BARRIER opcode is classified exactly",
    "[graphics][rdna2][barrier]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(10, 0x1234),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->format ==
        astraea::graphics::Rdna2InstructionFormat::sopp);
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::s_barrier);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 10);
    REQUIRE(result->sopp->simm16 == 0x1234);
    REQUIRE(result->raw_word == words[0]);
}


TEST_CASE(
    "RDNA2 SOPP S_WAITCNT opcode is classified exactly",
    "[graphics][rdna2][waitcnt]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(12, 0xaa35),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->format ==
        astraea::graphics::Rdna2InstructionFormat::sopp);
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::s_waitcnt);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 12);
    REQUIRE(
        static_cast<std::uint16_t>(
            result->sopp->simm16) ==
        0xaa35U);
    REQUIRE(result->raw_word == words[0]);
}

TEST_CASE(
    "neighboring S_SETKILL opcode remains explicit unknown",
    "[graphics][rdna2][waitcnt]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(11, 0),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);

    REQUIRE(result.has_value());
    REQUIRE(
        result->kind ==
        astraea::graphics::Rdna2InstructionKind::
            unknown_sopp_opcode);
    REQUIRE(result->sopp.has_value());
    REQUIRE(result->sopp->opcode == 11);
}
