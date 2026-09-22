#include <astraea/graphics/pm4_type3_framing.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace astraea::graphics {
namespace {

constexpr std::uint32_t kTypeMask = 0x3U;
constexpr unsigned int kTypeShift = 30U;
constexpr std::uint32_t kCountMask = 0x3fffU;
constexpr unsigned int kCountShift = 16U;
constexpr std::uint32_t kOpcodeMask = 0xffU;
constexpr unsigned int kOpcodeShift = 8U;
constexpr std::uint32_t kLowControlMask = 0xffU;

[[nodiscard]] Pm4Type3FrameError frame_error(
    Pm4Type3FrameErrorCode code,
    std::size_t word_offset,
    std::optional<std::uint8_t> header_type = std::nullopt,
    std::size_t required_words = 0,
    std::size_t available_words = 0,
    std::optional<PacketError> packet_error =
        std::nullopt) noexcept {
    return Pm4Type3FrameError{
        .code = code,
        .word_offset = word_offset,
        .header_type = header_type,
        .required_words = required_words,
        .available_words = available_words,
        .packet_error = packet_error,
    };
}

[[nodiscard]] std::uint32_t read_little_endian_word(
    std::span<const std::byte> bytes,
    std::size_t word_offset) noexcept {
    const auto byte_offset =
        word_offset * kPacketWordBytes;

    std::uint32_t value = 0;
    for (std::size_t index = 0;
         index < kPacketWordBytes;
         ++index) {
        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    bytes[byte_offset + index]))
            << (index * 8U);
    }
    return value;
}

}  // namespace

Pm4Type3FrameResult frame_pm4_type3_stream(
    std::span<const std::byte> command_buffer) {
    if (command_buffer.size() %
            kPacketWordBytes !=
        0U) {
        return Pm4Type3FrameResult::failure(
            frame_error(
                Pm4Type3FrameErrorCode::
                    command_buffer_not_word_aligned,
                0));
    }

    const auto stream_word_count =
        command_buffer.size() /
        kPacketWordBytes;
    std::size_t word_offset = 0;

    try {
        Pm4Type3Stream stream;

        while (word_offset < stream_word_count) {
            const auto raw_header =
                read_little_endian_word(
                    command_buffer,
                    word_offset);
            const auto header_type =
                static_cast<std::uint8_t>(
                    (raw_header >> kTypeShift) &
                    kTypeMask);
            const auto available_words =
                stream_word_count - word_offset;

            if (header_type !=
                kPm4Type3PacketType) {
                return Pm4Type3FrameResult::failure(
                    frame_error(
                        Pm4Type3FrameErrorCode::
                            unsupported_packet_type,
                        word_offset,
                        header_type,
                        1U,
                        available_words));
            }

            const auto encoded_count =
                static_cast<std::uint16_t>(
                    (raw_header >> kCountShift) &
                    kCountMask);

            const auto encoded_as_size =
                static_cast<std::size_t>(
                    encoded_count);
            if (encoded_as_size >
                std::numeric_limits<std::size_t>::max() -
                    2U) {
                return Pm4Type3FrameResult::failure(
                    frame_error(
                        Pm4Type3FrameErrorCode::
                            host_size_unrepresentable,
                        word_offset,
                        header_type,
                        0U,
                        available_words));
            }

            const auto body_word_count =
                encoded_as_size + 1U;
            const auto total_word_count =
                encoded_as_size + 2U;

            if (total_word_count >
                available_words) {
                return Pm4Type3FrameResult::failure(
                    frame_error(
                        Pm4Type3FrameErrorCode::
                            truncated_packet,
                        word_offset,
                        header_type,
                        total_word_count,
                        available_words));
            }

            auto raw_packet =
                parse_raw_packet(
                    command_buffer,
                    PacketExtent{
                        .word_offset =
                            word_offset,
                        .word_count =
                            total_word_count,
                    });
            if (!raw_packet.has_value()) {
                return Pm4Type3FrameResult::failure(
                    frame_error(
                        Pm4Type3FrameErrorCode::
                            raw_packet_failure,
                        word_offset,
                        header_type,
                        total_word_count,
                        available_words,
                        raw_packet.error()));
            }

            stream.frames.push_back(
                Pm4Type3Frame{
                    .word_offset = word_offset,
                    .header =
                        Pm4Type3Header{
                            .raw_word = raw_header,
                            .opcode =
                                static_cast<std::uint8_t>(
                                    (raw_header >>
                                     kOpcodeShift) &
                                    kOpcodeMask),
                            .encoded_count =
                                encoded_count,
                            .low_control_bits =
                                static_cast<std::uint8_t>(
                                    raw_header &
                                    kLowControlMask),
                            .body_word_count =
                                body_word_count,
                            .total_word_count =
                                total_word_count,
                        },
                    .raw_packet =
                        std::move(raw_packet).value(),
                });

            word_offset += total_word_count;
        }

        return Pm4Type3FrameResult::success(
            std::move(stream));
    } catch (const std::bad_alloc&) {
        return Pm4Type3FrameResult::failure(
            frame_error(
                Pm4Type3FrameErrorCode::
                    host_allocation_failure,
                word_offset));
    } catch (const std::length_error&) {
        return Pm4Type3FrameResult::failure(
            frame_error(
                Pm4Type3FrameErrorCode::
                    host_size_unrepresentable,
                word_offset));
    }
}

}  // namespace astraea::graphics
