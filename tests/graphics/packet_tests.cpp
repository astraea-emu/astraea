#include <astraea/graphics/packet.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using RawWord =
    std::array<
        std::byte,
        astraea::graphics::kPacketWordBytes>;

[[nodiscard]] std::vector<std::byte>
make_command_buffer() {
    return {
        std::byte{0x10},
        std::byte{0x11},
        std::byte{0x12},
        std::byte{0x13},
        std::byte{0x20},
        std::byte{0x21},
        std::byte{0x22},
        std::byte{0x23},
        std::byte{0x30},
        std::byte{0x31},
        std::byte{0x32},
        std::byte{0x33},
    };
}

}  // namespace

TEST_CASE(
    "raw graphics packet preserves words without invented semantics",
    "[graphics][packet]") {
    const auto bytes = make_command_buffer();

    const auto result =
        astraea::graphics::parse_raw_packet(
            bytes,
            astraea::graphics::PacketExtent{
                .word_offset = 1,
                .word_count = 2,
            });

    REQUIRE(result.has_value());
    REQUIRE(
        result->kind ==
        astraea::graphics::PacketKind::unknown);
    REQUIRE(result->extent.word_offset == 1);
    REQUIRE(result->extent.word_count == 2);
    REQUIRE(result->header.word_offset == 1);
    REQUIRE(result->raw_words.size() == 2);

    const RawWord first{
        std::byte{0x20},
        std::byte{0x21},
        std::byte{0x22},
        std::byte{0x23},
    };
    const RawWord second{
        std::byte{0x30},
        std::byte{0x31},
        std::byte{0x32},
        std::byte{0x33},
    };

    REQUIRE(result->raw_words[0].bytes == first);
    REQUIRE(result->raw_words[1].bytes == second);
    REQUIRE(result->header.raw == result->raw_words.front());
}

TEST_CASE(
    "raw packet parser rejects non-word-aligned command buffers",
    "[graphics][packet]") {
    const std::vector<std::byte> bytes(
        5,
        std::byte{0});

    const auto result =
        astraea::graphics::parse_raw_packet(
            bytes,
            astraea::graphics::PacketExtent{
                .word_offset = 0,
                .word_count = 1,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::PacketErrorCode::
            command_buffer_not_word_aligned);
}

TEST_CASE(
    "raw packet parser rejects empty packet extents",
    "[graphics][packet]") {
    const auto bytes = make_command_buffer();

    const auto result =
        astraea::graphics::parse_raw_packet(
            bytes,
            astraea::graphics::PacketExtent{
                .word_offset = 0,
                .word_count = 0,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::PacketErrorCode::
            empty_packet);
}

TEST_CASE(
    "raw packet parser rejects overflowing packet extents",
    "[graphics][packet]") {
    const auto bytes = make_command_buffer();

    const auto result =
        astraea::graphics::parse_raw_packet(
            bytes,
            astraea::graphics::PacketExtent{
                .word_offset =
                    std::numeric_limits<std::size_t>::max(),
                .word_count = 2,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::PacketErrorCode::
            packet_range_overflow);
}

TEST_CASE(
    "raw packet parser rejects out-of-bounds packet extents",
    "[graphics][packet]") {
    const auto bytes = make_command_buffer();

    SECTION("start is past command buffer") {
        const auto result =
            astraea::graphics::parse_raw_packet(
                bytes,
                astraea::graphics::PacketExtent{
                    .word_offset = 3,
                    .word_count = 1,
                });

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::PacketErrorCode::
                packet_range_out_of_bounds);
    }

    SECTION("packet extends past command buffer") {
        const auto result =
            astraea::graphics::parse_raw_packet(
                bytes,
                astraea::graphics::PacketExtent{
                    .word_offset = 2,
                    .word_count = 2,
                });

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::PacketErrorCode::
                packet_range_out_of_bounds);
    }
}

TEST_CASE(
    "raw packet parser accepts a packet ending at the buffer boundary",
    "[graphics][packet]") {
    const auto bytes = make_command_buffer();

    const auto result =
        astraea::graphics::parse_raw_packet(
            bytes,
            astraea::graphics::PacketExtent{
                .word_offset = 2,
                .word_count = 1,
            });

    REQUIRE(result.has_value());
    REQUIRE(result->raw_words.size() == 1);
    REQUIRE(result->header.raw == result->raw_words.front());
}
