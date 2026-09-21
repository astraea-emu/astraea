#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/packet.hpp>

#include <cstddef>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "unknown packet emits typed unsupported Graphics IR with exact provenance",
    "[graphics][ir]") {
    const std::vector<std::byte> command_buffer{
        std::byte{0x10},
        std::byte{0x11},
        std::byte{0x12},
        std::byte{0x13},
        std::byte{0x20},
        std::byte{0x21},
        std::byte{0x22},
        std::byte{0x23},
    };

    auto packet =
        astraea::graphics::parse_raw_packet(
            command_buffer,
            astraea::graphics::PacketExtent{
                .word_offset = 1,
                .word_count = 1,
            });

    REQUIRE(packet.has_value());
    const auto expected_packet = packet.value();

    const auto emission =
        astraea::graphics::make_unsupported_packet_ir(
            std::move(packet).value());

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::GraphicsIrUnsupported>(
            emission.operation));

    const auto& unsupported =
        std::get<
            astraea::graphics::GraphicsIrUnsupported>(
            emission.operation);

    REQUIRE(
        unsupported.reason ==
        astraea::graphics::GraphicsIrUnsupportedReason::
            packet_semantics_unknown);
    REQUIRE(
        emission.provenance.source_packet ==
        expected_packet);
}

TEST_CASE(
    "Graphics IR semantic equality excludes raw packet provenance",
    "[graphics][ir]") {
    const std::vector<std::byte> left_buffer{
        std::byte{0x10},
        std::byte{0x11},
        std::byte{0x12},
        std::byte{0x13},
    };
    const std::vector<std::byte> right_buffer{
        std::byte{0x20},
        std::byte{0x21},
        std::byte{0x22},
        std::byte{0x23},
    };

    auto left_packet =
        astraea::graphics::parse_raw_packet(
            left_buffer,
            astraea::graphics::PacketExtent{
                .word_offset = 0,
                .word_count = 1,
            });
    auto right_packet =
        astraea::graphics::parse_raw_packet(
            right_buffer,
            astraea::graphics::PacketExtent{
                .word_offset = 0,
                .word_count = 1,
            });

    REQUIRE(left_packet.has_value());
    REQUIRE(right_packet.has_value());

    const auto left =
        astraea::graphics::make_unsupported_packet_ir(
            std::move(left_packet).value());
    const auto right =
        astraea::graphics::make_unsupported_packet_ir(
            std::move(right_packet).value());

    REQUIRE(
        left.provenance.source_packet !=
        right.provenance.source_packet);
    REQUIRE(
        astraea::graphics::graphics_ir_semantically_equal(
            left,
            right));
}
