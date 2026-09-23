#include <astraea/graphics/pm4_set_context_reg_ir.hpp>

#include <array>
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
    for (std::size_t index = 0;
         index < 4U;
         ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) &
                    0xffU)});
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
            lower_pm4_set_context_reg_frame_to_graphics_ir(
                frame);
    REQUIRE(lowered.has_value());
    return std::move(lowered).value();
}

const astraea::graphics::GraphicsIrContextRegisterWriteRange&
require_context_range(
    const astraea::graphics::GraphicsIrEmission& emission) {
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::
                GraphicsIrContextRegisterWriteRange>(
            emission.operation));
    return std::get<
        astraea::graphics::GraphicsIrContextRegisterWriteRange>(
        emission.operation);
}

}  // namespace

TEST_CASE(
    "SET_CONTEXT_REG one-value packet lowers relative offset and exact value",
    "[graphics][pm4][set-context-reg]") {
    constexpr std::uint32_t value = 0x78563412U;
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U),
            0x0010U,
            value,
        });

    const auto& operation =
        require_context_range(emission);
    REQUIRE(operation.start_offset == 0x0010U);
    REQUIRE(operation.values.size() == 1U);
    REQUIRE(operation.values[0] == value);
    REQUIRE(
        emission.provenance.source_packet.raw_words[2]
            .bytes ==
        std::array<std::byte, 4>{
            std::byte{0x12},
            std::byte{0x34},
            std::byte{0x56},
            std::byte{0x78}});
}

TEST_CASE(
    "SET_CONTEXT_REG multi-value packet preserves consecutive values",
    "[graphics][pm4][set-context-reg]") {
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                3U),
            0x0020U,
            0x01020304U,
            0x11121314U,
            0xaabbccddU,
        });

    const auto& operation =
        require_context_range(emission);
    REQUIRE(operation.start_offset == 0x0020U);
    REQUIRE(
        operation.values ==
        std::vector<std::uint32_t>{
            0x01020304U,
            0x11121314U,
            0xaabbccddU});
}

TEST_CASE(
    "non-SET_CONTEXT_REG Type-3 opcode remains unsupported Graphics IR",
    "[graphics][pm4][set-context-reg][unsupported]") {
    const auto emission =
        lower_one({
            make_type3_header(0x68U, 1U),
            0U,
            0x12345678U,
        });

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::GraphicsIrUnsupported>(
            emission.operation));
    REQUIRE(
        std::get<
            astraea::graphics::GraphicsIrUnsupported>(
            emission.operation)
            .reason ==
        astraea::graphics::GraphicsIrUnsupportedReason::
            packet_semantics_unknown);
}

TEST_CASE(
    "SET_CONTEXT_REG without value payload fails explicitly",
    "[graphics][pm4][set-context-reg][negative]") {
    auto frame =
        frame_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                0U),
            0U,
        });

    const auto result =
        astraea::graphics::
            lower_pm4_set_context_reg_frame_to_graphics_ir(
                frame);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4SetContextRegLowerErrorCode::
                malformed_set_context_reg);
    REQUIRE(result.error().value_count == 0U);
}

TEST_CASE(
    "SET_CONTEXT_REG rejects unsupported upper control bits",
    "[graphics][pm4][set-context-reg][negative]") {
    auto frame =
        frame_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U),
            0x00010012U,
            0x12345678U,
        });

    const auto result =
        astraea::graphics::
            lower_pm4_set_context_reg_frame_to_graphics_ir(
                frame);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4SetContextRegLowerErrorCode::
                unsupported_register_control_bits);
    REQUIRE(
        result.error().raw_offset_control_word ==
        0x00010012U);
}

TEST_CASE(
    "SET_CONTEXT_REG accepts range ending at final context register",
    "[graphics][pm4][set-context-reg][boundary]") {
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U),
            0x03ffU,
            0xabcdef01U,
        });

    const auto& operation =
        require_context_range(emission);
    REQUIRE(operation.start_offset == 0x03ffU);
    REQUIRE(operation.values.size() == 1U);
}

TEST_CASE(
    "SET_CONTEXT_REG rejects range crossing context register window",
    "[graphics][pm4][set-context-reg][boundary][negative]") {
    auto frame =
        frame_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                2U),
            0x03ffU,
            0x11111111U,
            0x22222222U,
        });

    const auto result =
        astraea::graphics::
            lower_pm4_set_context_reg_frame_to_graphics_ir(
                frame);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4SetContextRegLowerErrorCode::
                register_range_out_of_bounds);
    REQUIRE(result.error().value_count == 2U);
}

TEST_CASE(
    "SET_CONTEXT_REG semantic equality ignores raw packet provenance",
    "[graphics][pm4][set-context-reg][equality]") {
    const auto left =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U,
                0x01U),
            0x0020U,
            0x12345678U,
        });
    const auto right =
        lower_one({
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U,
                0x7fU),
            0x0020U,
            0x12345678U,
        });

    REQUIRE(
        left.provenance.source_packet !=
        right.provenance.source_packet);
    REQUIRE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left,
                right));

    auto changed_offset = right;
    std::get<
        astraea::graphics::GraphicsIrContextRegisterWriteRange>(
        changed_offset.operation)
        .start_offset = 0x21U;
    REQUIRE_FALSE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left,
                changed_offset));

    auto changed_value = right;
    std::get<
        astraea::graphics::GraphicsIrContextRegisterWriteRange>(
        changed_value.operation)
        .values[0] = 0x87654321U;
    REQUIRE_FALSE(
        astraea::graphics::
            graphics_ir_semantically_equal(
                left,
                changed_value));
}

TEST_CASE(
    "framed PM4 stream preserves SET_CONTEXT_REG packet provenance",
    "[graphics][pm4][set-context-reg][handoff]") {
    const auto framed =
        astraea::graphics::frame_pm4_type3_stream(
            make_stream({
                make_type3_header(0x70U, 0U),
                0x11111111U,
                make_type3_header(
                    astraea::graphics::
                        kPm4SetContextRegOpcode,
                    2U),
                0x0010U,
                0x11223344U,
                0x55667788U,
            }));
    REQUIRE(framed.has_value());
    REQUIRE(framed->frames.size() == 2U);

    const auto first =
        astraea::graphics::
            lower_pm4_set_context_reg_frame_to_graphics_ir(
                framed->frames[0]);
    const auto second =
        astraea::graphics::
            lower_pm4_set_context_reg_frame_to_graphics_ir(
                framed->frames[1]);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::GraphicsIrUnsupported>(
            first->operation));

    const auto& operation =
        require_context_range(second.value());
    REQUIRE(operation.start_offset == 0x0010U);
    REQUIRE(
        operation.values ==
        std::vector<std::uint32_t>{
            0x11223344U,
            0x55667788U});
    REQUIRE(
        second->provenance.source_packet.extent.word_offset ==
        2U);
}
