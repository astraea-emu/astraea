#include <astraea/graphics/packet.hpp>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::graphics {
namespace {

[[nodiscard]] PacketError packet_error(
    PacketErrorCode code,
    PacketExtent extent) noexcept {
    return PacketError{
        .code = code,
        .word_offset = extent.word_offset,
        .word_count = extent.word_count,
    };
}

[[nodiscard]] RawPacketWord read_raw_word(
    std::span<const std::byte> command_buffer,
    std::size_t word_index) noexcept {
    const auto byte_offset =
        word_index * kPacketWordBytes;

    return RawPacketWord{
        .bytes = {
            command_buffer[byte_offset],
            command_buffer[byte_offset + 1U],
            command_buffer[byte_offset + 2U],
            command_buffer[byte_offset + 3U],
        },
    };
}

}  // namespace

PacketParseResult parse_raw_packet(
    std::span<const std::byte> command_buffer,
    PacketExtent extent) {
    if (command_buffer.size() % kPacketWordBytes != 0U) {
        return PacketParseResult::failure(
            packet_error(
                PacketErrorCode::
                    command_buffer_not_word_aligned,
                extent));
    }

    if (extent.word_count == 0U) {
        return PacketParseResult::failure(
            packet_error(
                PacketErrorCode::empty_packet,
                extent));
    }

    if (extent.word_offset >
        std::numeric_limits<std::size_t>::max() -
            extent.word_count) {
        return PacketParseResult::failure(
            packet_error(
                PacketErrorCode::packet_range_overflow,
                extent));
    }

    const auto packet_end =
        extent.word_offset + extent.word_count;
    const auto available_words =
        command_buffer.size() / kPacketWordBytes;

    if (extent.word_offset >= available_words ||
        packet_end > available_words) {
        return PacketParseResult::failure(
            packet_error(
                PacketErrorCode::
                    packet_range_out_of_bounds,
                extent));
    }

    try {
        std::vector<RawPacketWord> raw_words;
        raw_words.reserve(extent.word_count);

        for (std::size_t index = 0;
             index < extent.word_count;
             ++index) {
            raw_words.push_back(
                read_raw_word(
                    command_buffer,
                    extent.word_offset + index));
        }

        const PacketHeader header{
            .word_offset = extent.word_offset,
            .raw = raw_words.front(),
        };

        return PacketParseResult::success(
            RawPacket{
                .extent = extent,
                .header = header,
                .kind = PacketKind::unknown,
                .raw_words = std::move(raw_words),
            });
    } catch (const std::bad_alloc&) {
        return PacketParseResult::failure(
            packet_error(
                PacketErrorCode::host_allocation_failure,
                extent));
    } catch (const std::length_error&) {
        return PacketParseResult::failure(
            packet_error(
                PacketErrorCode::host_allocation_failure,
                extent));
    }
}

}  // namespace astraea::graphics
