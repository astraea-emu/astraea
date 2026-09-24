#include <astraea/graphics/pm4_set_uconfig_reg_ir.hpp>

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
    std::uint8_t low_control_bits = 0U) {
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
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) & 0xffU)});
    }
}

astraea::graphics::Pm4Type3Frame frame_one(
    std::initializer_list<std::uint32_t> words) {
    std::vector<std::byte> bytes;
    for (const auto word : words) {
        append_word(bytes, word);
    }

    auto framed =
        astraea::graphics::frame_pm4_type3_stream(bytes);
    REQUIRE(framed.has_value());
    REQUIRE(framed->frames.size() == 1U);
    return std::move(framed->frames.front());
}

astraea::graphics::GraphicsIrEmission lower_one(
    std::initializer_list<std::uint32_t> words) {
    auto frame = frame_one(words);
    auto lowered =
        astraea::graphics::
            lower_pm4_set_uconfig_reg_frame_to_graphics_ir(
                frame);
    REQUIRE(lowered.has_value());
    return std::move(lowered).value();
}

}  // namespace

TEST_CASE(
    "SET_UCONFIG_REG lowers relative register ranges exactly",
    "[graphics][pm4][set-uconfig-reg]") {
    const auto emission =
        lower_one({
            make_type3_header(
                astraea::graphics::kPm4SetUconfigRegOpcode,
                2U),
            0x025bU,
            0x00008040U,
            0x11223344U,
        });

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::
                GraphicsIrUserConfigRegisterWriteRange>(
            emission.operation));
    const auto& write =
        std::get<
            astraea::graphics::
                GraphicsIrUserConfigRegisterWriteRange>(
            emission.operation);
    REQUIRE(write.start_offset == 0x025bU);
    REQUIRE(
        write.values ==
        std::vector<std::uint32_t>{
            0x00008040U,
            0x11223344U});
}

TEST_CASE(
    "SET_UCONFIG_REG rejects controlled and out-of-window forms",
    "[graphics][pm4][set-uconfig-reg][negative]") {
    SECTION("control bits") {
        auto frame =
            frame_one({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetUconfigRegOpcode,
                    1U),
                0x20000242U,
                4U,
            });

        const auto result =
            astraea::graphics::
                lower_pm4_set_uconfig_reg_frame_to_graphics_ir(
                    frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Pm4SetUconfigRegLowerErrorCode::
                    unsupported_register_control_bits);
    }

    SECTION("range crossing end") {
        auto frame =
            frame_one({
                make_type3_header(
                    astraea::graphics::
                        kPm4SetUconfigRegOpcode,
                    2U),
                0x03ffU,
                1U,
                2U,
            });

        const auto result =
            astraea::graphics::
                lower_pm4_set_uconfig_reg_frame_to_graphics_ir(
                    frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                Pm4SetUconfigRegLowerErrorCode::
                    register_range_out_of_bounds);
    }
}

TEST_CASE(
    "SET_UCONFIG_REG leaves unrelated opcodes typed unsupported",
    "[graphics][pm4][set-uconfig-reg][unsupported]") {
    const auto emission =
        lower_one({
            make_type3_header(0x78U, 1U),
            0U,
            0x12345678U,
        });

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::GraphicsIrUnsupported>(
            emission.operation));
}

TEST_CASE(
    "SET_UCONFIG_REG semantic equality ignores raw packet provenance",
    "[graphics][pm4][set-uconfig-reg][equality]") {
    const auto left =
        lower_one({
            make_type3_header(
                astraea::graphics::kPm4SetUconfigRegOpcode,
                1U,
                0x01U),
            0x0242U,
            4U,
        });
    const auto right =
        lower_one({
            make_type3_header(
                astraea::graphics::kPm4SetUconfigRegOpcode,
                1U,
                0x7fU),
            0x0242U,
            4U,
        });

    REQUIRE(
        left.provenance.source_packet !=
        right.provenance.source_packet);
    REQUIRE(
        astraea::graphics::
            graphics_ir_semantically_equal(left, right));
}
