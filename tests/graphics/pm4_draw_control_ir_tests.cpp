#include <astraea/graphics/pm4_draw_control_ir.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <variant>
#include <vector>

#include <astraea/graphics/pm4_type3_framing.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t make_type3_header(
    std::uint8_t opcode,
    std::uint16_t encoded_count,
    std::uint8_t low_control_bits = 0) {
    return
        (static_cast<std::uint32_t>(
             astraea::graphics::kPm4Type3PacketType)
         << 30U) |
        ((static_cast<std::uint32_t>(encoded_count) &
          0x3fffU)
         << 16U) |
        (static_cast<std::uint32_t>(opcode) << 8U) |
        static_cast<std::uint32_t>(low_control_bits);
}

void append_word(
    std::vector<std::byte>& bytes,
    std::uint32_t word) {
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) & 0xffU)});
    }
}

std::vector<std::byte> make_stream(
    std::initializer_list<std::uint32_t> words) {
    std::vector<std::byte> bytes;
    bytes.reserve(words.size() * 4U);
    for (const auto word : words) {
        append_word(bytes, word);
    }
    return bytes;
}

astraea::graphics::Pm4Type3Frame frame_one(
    std::initializer_list<std::uint32_t> words) {
    auto framed =
        astraea::graphics::frame_pm4_type3_stream(
            make_stream(words));
    REQUIRE(framed.has_value());
    REQUIRE(framed->frames.size() == 1U);
    return std::move(framed->frames.front());
}

astraea::graphics::GraphicsIrEmission lower_one(
    std::initializer_list<std::uint32_t> words) {
    auto frame = frame_one(words);
    auto lowered =
        astraea::graphics::
            lower_pm4_draw_control_frame_to_graphics_ir(
                frame);
    REQUIRE(lowered.has_value());
    return std::move(lowered).value();
}

}  // namespace

TEST_CASE(
    "NUM_INSTANCES lowers its exact one-dword payload",
    "[graphics][pm4][draw-control][instances]") {
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            1U,
        });

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::
                GraphicsIrSetInstanceCount>(
            emission.operation));
    REQUIRE(
        std::get<
            astraea::graphics::
                GraphicsIrSetInstanceCount>(
            emission.operation)
            .instance_count == 1U);
}

TEST_CASE(
    "DRAW_INDEX_AUTO preserves count and opaque initiator exactly",
    "[graphics][pm4][draw-control][draw-index-auto]") {
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::
                GraphicsIrDrawIndexAuto>(
            emission.operation));
    const auto& draw =
        std::get<
            astraea::graphics::
                GraphicsIrDrawIndexAuto>(
            emission.operation);
    REQUIRE(draw.index_count == 3U);
    REQUIRE(draw.initiator == 2U);
}

TEST_CASE(
    "DRAW_INDEX_AUTO does not normalize unknown initiator bits",
    "[graphics][pm4][draw-control][draw-index-auto][opaque]") {
    constexpr std::uint32_t kOpaqueInitiator =
        0xa5a50002U;
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            6U,
            kOpaqueInitiator,
        });

    const auto& draw =
        std::get<
            astraea::graphics::
                GraphicsIrDrawIndexAuto>(
            emission.operation);
    REQUIRE(draw.index_count == 6U);
    REQUIRE(draw.initiator == kOpaqueInitiator);
}

TEST_CASE(
    "draw-control lowerer leaves unrelated opcodes unsupported",
    "[graphics][pm4][draw-control][unsupported]") {
    const auto emission =
        lower_one({
            make_type3_header(0x2aU, 0U),
            0U,
        });

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::GraphicsIrUnsupported>(
            emission.operation));
}

TEST_CASE(
    "draw-control packet shapes are exact",
    "[graphics][pm4][draw-control][negative]") {
    SECTION("NUM_INSTANCES with extra payload") {
        auto frame =
            frame_one({
                make_type3_header(
                    astraea::graphics::
                        kPm4NumInstancesOpcode,
                    1U),
                1U,
                0xdeadbeefU,
            });

        const auto result =
            astraea::graphics::
                lower_pm4_draw_control_frame_to_graphics_ir(
                    frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Pm4DrawControlLowerErrorCode::
                    malformed_num_instances);
        REQUIRE(
            result.error().expected_total_word_count ==
            2U);
        REQUIRE(
            result.error().actual_total_word_count ==
            3U);
    }

    SECTION("DRAW_INDEX_AUTO missing initiator") {
        auto frame =
            frame_one({
                make_type3_header(
                    astraea::graphics::
                        kPm4DrawIndexAutoOpcode,
                    0U),
                3U,
            });

        const auto result =
            astraea::graphics::
                lower_pm4_draw_control_frame_to_graphics_ir(
                    frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Pm4DrawControlLowerErrorCode::
                    malformed_draw_index_auto);
        REQUIRE(
            result.error().expected_total_word_count ==
            3U);
        REQUIRE(
            result.error().actual_total_word_count ==
            2U);
    }
}

TEST_CASE(
    "draw-control semantic equality ignores packet provenance",
    "[graphics][pm4][draw-control][equality]") {
    const auto left =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U,
                0x01U),
            3U,
            2U,
        });
    const auto right =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U,
                0x7fU),
            3U,
            2U,
        });

    REQUIRE(
        left.provenance.source_packet !=
        right.provenance.source_packet);
    REQUIRE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left,
                right));

    auto changed = right;
    std::get<
        astraea::graphics::
            GraphicsIrDrawIndexAuto>(
        changed.operation)
        .index_count = 4U;
    REQUIRE_FALSE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left,
                changed));
}
