#include <astraea/graphics/pm4_type3_framing.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <astraea/execution/sce_agc_driver_submit_dcb.hpp>

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
        ((static_cast<std::uint32_t>(
              encoded_count) &
          0x3fffU)
         << 16U) |
        (static_cast<std::uint32_t>(opcode)
         << 8U) |
        static_cast<std::uint32_t>(
            low_control_bits);
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

std::array<std::byte, 4> word_bytes(
    std::uint32_t word) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) &
                    0xffU)};
    }
    return bytes;
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

}  // namespace

TEST_CASE(
    "empty PM4 Type-3 stream frames successfully",
    "[graphics][pm4][framing]") {
    const std::array<std::byte, 0> bytes{};

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->frames.empty());
}

TEST_CASE(
    "minimal PM4 Type-3 packet preserves structural header and raw bytes",
    "[graphics][pm4][framing]") {
    constexpr auto header =
        make_type3_header(
            0x7fU,
            0U,
            0xa5U);
    constexpr std::uint32_t payload =
        0xdeadbeefU;
    const auto bytes =
        make_stream({
            header,
            payload,
        });

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->frames.size() == 1U);

    const auto& frame = result->frames.front();
    REQUIRE(frame.word_offset == 0U);
    REQUIRE(frame.header.raw_word == header);
    REQUIRE(frame.header.opcode == 0x7fU);
    REQUIRE(frame.header.encoded_count == 0U);
    REQUIRE(frame.header.low_control_bits == 0xa5U);
    REQUIRE(frame.header.body_word_count == 1U);
    REQUIRE(frame.header.total_word_count == 2U);

    REQUIRE(
        frame.raw_packet.kind ==
        astraea::graphics::PacketKind::unknown);
    REQUIRE(frame.raw_packet.extent.word_offset == 0U);
    REQUIRE(frame.raw_packet.extent.word_count == 2U);
    REQUIRE(frame.raw_packet.raw_words.size() == 2U);
    REQUIRE(
        frame.raw_packet.raw_words[0].bytes ==
        word_bytes(header));
    REQUIRE(
        frame.raw_packet.raw_words[1].bytes ==
        word_bytes(payload));
}

TEST_CASE(
    "PM4 Type-3 framing assigns no opcode semantics",
    "[graphics][pm4][framing][semantics]") {
    const auto first =
        make_stream({
            make_type3_header(0x10U, 0U),
            0x11111111U,
        });
    const auto second =
        make_stream({
            make_type3_header(0xfeU, 0U),
            0x22222222U,
        });

    const auto framed_first =
        astraea::graphics::
            frame_pm4_type3_stream(first);
    const auto framed_second =
        astraea::graphics::
            frame_pm4_type3_stream(second);

    REQUIRE(framed_first.has_value());
    REQUIRE(framed_second.has_value());
    REQUIRE(
        framed_first->frames[0].header.opcode ==
        0x10U);
    REQUIRE(
        framed_second->frames[0].header.opcode ==
        0xfeU);
    REQUIRE(
        framed_first->frames[0].raw_packet.kind ==
        astraea::graphics::PacketKind::unknown);
    REQUIRE(
        framed_second->frames[0].raw_packet.kind ==
        astraea::graphics::PacketKind::unknown);
}

TEST_CASE(
    "multiple PM4 Type-3 packets retain deterministic word offsets",
    "[graphics][pm4][framing]") {
    const auto bytes =
        make_stream({
            make_type3_header(0x21U, 0U, 0x01U),
            0xaaaaaaaaU,
            make_type3_header(0x22U, 1U, 0x02U),
            0xbbbbbbbbU,
            0xccccccccU,
        });

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->frames.size() == 2U);
    REQUIRE(result->frames[0].word_offset == 0U);
    REQUIRE(
        result->frames[0].header.total_word_count ==
        2U);
    REQUIRE(result->frames[1].word_offset == 2U);
    REQUIRE(
        result->frames[1].header.encoded_count ==
        1U);
    REQUIRE(
        result->frames[1].header.body_word_count ==
        2U);
    REQUIRE(
        result->frames[1].header.total_word_count ==
        3U);
    REQUIRE(
        result->frames[1].raw_packet.extent.word_offset ==
        2U);
    REQUIRE(
        result->frames[1].raw_packet.extent.word_count ==
        3U);
}

TEST_CASE(
    "non-Type-3 header fails at its exact word offset",
    "[graphics][pm4][framing][negative]") {
    const auto bytes =
        make_stream({
            make_type3_header(0x20U, 0U),
            0x11111111U,
            2U << 30U,
            0x22222222U,
        });

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4Type3FrameErrorCode::
                unsupported_packet_type);
    REQUIRE(result.error().word_offset == 2U);
    REQUIRE(
        result.error().header_type ==
        std::optional<std::uint8_t>{2U});
    REQUIRE(result.error().required_words == 1U);
    REQUIRE(result.error().available_words == 2U);
}

TEST_CASE(
    "truncated PM4 Type-3 packet reports required and available words",
    "[graphics][pm4][framing][negative]") {
    const auto bytes =
        make_stream({
            make_type3_header(0x33U, 2U),
            0x12345678U,
        });

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4Type3FrameErrorCode::
                truncated_packet);
    REQUIRE(result.error().word_offset == 0U);
    REQUIRE(result.error().required_words == 4U);
    REQUIRE(result.error().available_words == 2U);
}

TEST_CASE(
    "truncation after a complete packet reports the next packet",
    "[graphics][pm4][framing][negative]") {
    const auto bytes =
        make_stream({
            make_type3_header(0x44U, 0U),
            0xaaaaaaaaU,
            make_type3_header(0x55U, 1U),
            0xbbbbbbbbU,
        });

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4Type3FrameErrorCode::
                truncated_packet);
    REQUIRE(result.error().word_offset == 2U);
    REQUIRE(result.error().required_words == 3U);
    REQUIRE(result.error().available_words == 2U);
}

TEST_CASE(
    "non-word-aligned PM4 stream rejects before framing",
    "[graphics][pm4][framing][negative]") {
    const std::array bytes{
        std::byte{0},
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
    };

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            Pm4Type3FrameErrorCode::
                command_buffer_not_word_aligned);
    REQUIRE(result.error().word_offset == 0U);
    REQUIRE_FALSE(result.error().header_type.has_value());
    REQUIRE_FALSE(result.error().packet_error.has_value());
}

TEST_CASE(
    "captured AGC DCB bytes hand off directly to PM4 Type-3 framing",
    "[execution][agc][graphics][pm4][framing]") {
    auto bytes =
        make_stream({
            make_type3_header(0x61U, 0U, 0x03U),
            0x01020304U,
            make_type3_header(0x62U, 1U, 0x04U),
            0x11223344U,
            0x55667788U,
        });

    astraea::execution::SceAgcDcbSubmission
        submission{
            .submit_description_address =
                astraea::memory::GuestAddress{
                    0x1000U},
            .command_words_address =
                astraea::memory::GuestAddress{
                    0x2000U},
            .word_count = 5U,
            .flag = 0U,
            .raw_submit_description = {},
            .opaque_padding = {},
            .command_buffer_bytes =
                std::move(bytes),
        };

    const auto result =
        astraea::graphics::
            frame_pm4_type3_stream(
                submission.command_buffer_bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->frames.size() == 2U);
    REQUIRE(result->frames[0].word_offset == 0U);
    REQUIRE(result->frames[1].word_offset == 2U);
    REQUIRE(
        submission.command_buffer_bytes.size() ==
        submission.word_count * 4U);
}
