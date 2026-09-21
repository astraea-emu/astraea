#include <astraea/graphics/pm4_frontend.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t make_type3_header(
    std::uint8_t opcode,
    std::uint16_t encoded_count,
    std::uint8_t reserved_low = 0) {
    return (static_cast<std::uint32_t>(
                astraea::graphics::kPm4Type3)
            << 30U) |
           ((static_cast<std::uint32_t>(
                 encoded_count) &
             0x3fffU)
            << 16U) |
           (static_cast<std::uint32_t>(opcode)
            << 8U) |
           static_cast<std::uint32_t>(
               reserved_low);
}

}  // namespace

TEST_CASE(
    "empty PM4 command buffer parses",
    "[graphics][pm4]") {
    const std::array<std::uint32_t, 0> words{};

    const auto result =
        astraea::graphics::parse_generic_pm4_type3(
            words);

    REQUIRE(result.has_value());
    REQUIRE(result->packets.empty());
}

TEST_CASE(
    "generic PM4 type3 header is decoded and raw words are preserved",
    "[graphics][pm4]") {
    const std::array<std::uint32_t, 2> words{
        make_type3_header(
            astraea::graphics::kPm4GenericNopOpcode,
            0,
            0xa5),
        0xdeadbeefU,
    };

    const auto result =
        astraea::graphics::parse_generic_pm4_type3(
            words);

    REQUIRE(result.has_value());
    REQUIRE(result->packets.size() == 1);

    const auto& packet = result->packets.front();
    REQUIRE(packet.word_index == 0);
    REQUIRE(
        packet.kind ==
        astraea::graphics::Pm4PacketKind::generic_nop);
    REQUIRE(packet.header.raw_word == words[0]);
    REQUIRE(
        packet.header.type ==
        astraea::graphics::kPm4Type3);
    REQUIRE(
        packet.header.opcode ==
        astraea::graphics::kPm4GenericNopOpcode);
    REQUIRE(packet.header.encoded_count == 0);
    REQUIRE(packet.header.reserved_low == 0xa5);
    REQUIRE(packet.header.body_word_count == 1);
    REQUIRE(packet.header.total_word_count == 2);
    REQUIRE(packet.raw_words.size() == 2);
    REQUIRE(packet.raw_words[0] == words[0]);
    REQUIRE(packet.raw_words[1] == words[1]);
}

TEST_CASE(
    "unknown type3 opcode remains observable without invented semantics",
    "[graphics][pm4]") {
    const std::array<std::uint32_t, 3> words{
        make_type3_header(0x7f, 1),
        0x11111111U,
        0x22222222U,
    };

    const auto result =
        astraea::graphics::parse_generic_pm4_type3(
            words);

    REQUIRE(result.has_value());
    REQUIRE(result->packets.size() == 1);

    const auto& packet = result->packets.front();
    REQUIRE(
        packet.kind ==
        astraea::graphics::Pm4PacketKind::unknown);
    REQUIRE(packet.header.opcode == 0x7f);
    REQUIRE(packet.header.body_word_count == 2);
    REQUIRE(packet.header.total_word_count == 3);
    REQUIRE(
        packet.raw_words ==
        std::vector<std::uint32_t>{
            words.begin(),
            words.end()});
}

TEST_CASE(
    "multiple type3 packets retain deterministic word offsets",
    "[graphics][pm4]") {
    const std::array<std::uint32_t, 5> words{
        make_type3_header(0x21, 0),
        0xaaaaaaaaU,
        make_type3_header(0x22, 1),
        0xbbbbbbbbU,
        0xccccccccU,
    };

    const auto result =
        astraea::graphics::parse_generic_pm4_type3(
            words);

    REQUIRE(result.has_value());
    REQUIRE(result->packets.size() == 2);
    REQUIRE(result->packets[0].word_index == 0);
    REQUIRE(result->packets[0].header.opcode == 0x21);
    REQUIRE(result->packets[1].word_index == 2);
    REQUIRE(result->packets[1].header.opcode == 0x22);
    REQUIRE(result->packets[1].raw_words.size() == 3);
}

TEST_CASE(
    "unsupported PM4 header type fails deterministically",
    "[graphics][pm4]") {
    const std::array<std::uint32_t, 2> words{
        2U << 30U,
        0x12345678U,
    };

    const auto result =
        astraea::graphics::parse_generic_pm4_type3(
            words);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::Pm4ParseErrorCode::
            unsupported_packet_type);
    REQUIRE(result.error().word_index == 0);
    REQUIRE(result.error().header_type == 2);
    REQUIRE(result.error().required_words == 1);
    REQUIRE(result.error().available_words == 2);
}

TEST_CASE(
    "truncated type3 packet reports required and available words",
    "[graphics][pm4]") {
    const std::array<std::uint32_t, 2> words{
        make_type3_header(0x33, 2),
        0x12345678U,
    };

    const auto result =
        astraea::graphics::parse_generic_pm4_type3(
            words);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::Pm4ParseErrorCode::
            truncated_packet);
    REQUIRE(result.error().word_index == 0);
    REQUIRE(
        result.error().header_type ==
        astraea::graphics::kPm4Type3);
    REQUIRE(result.error().required_words == 4);
    REQUIRE(result.error().available_words == 2);
}

TEST_CASE(
    "truncation after a complete packet reports the next packet offset",
    "[graphics][pm4]") {
    const std::array<std::uint32_t, 4> words{
        make_type3_header(0x44, 0),
        0xaaaaaaaaU,
        make_type3_header(0x55, 1),
        0xbbbbbbbbU,
    };

    const auto result =
        astraea::graphics::parse_generic_pm4_type3(
            words);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::Pm4ParseErrorCode::
            truncated_packet);
    REQUIRE(result.error().word_index == 2);
    REQUIRE(result.error().required_words == 3);
    REQUIRE(result.error().available_words == 2);
}
