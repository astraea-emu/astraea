#include <astraea/graphics/pm4_draw_control_ir.hpp>

#include <cstddef>
#include <cstdint>

namespace astraea::graphics {
namespace {

[[nodiscard]] std::uint32_t raw_word_value(
    const RawPacketWord& word) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0;
         index < word.bytes.size();
         ++index) {
        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    word.bytes[index]))
            << (index * 8U);
    }
    return value;
}

[[nodiscard]] Pm4DrawControlLowerError lower_error(
    Pm4DrawControlLowerErrorCode code,
    const Pm4Type3Frame& frame,
    std::size_t expected_total_word_count) noexcept {
    return Pm4DrawControlLowerError{
        .code = code,
        .word_offset = frame.word_offset,
        .expected_total_word_count =
            expected_total_word_count,
        .actual_total_word_count =
            frame.raw_packet.raw_words.size(),
    };
}

}  // namespace

Pm4DrawControlLowerResult
lower_pm4_draw_control_frame_to_graphics_ir(
    const Pm4Type3Frame& frame) noexcept {
    if (frame.header.opcode == kPm4NumInstancesOpcode) {
        constexpr std::size_t kExpectedWords = 2U;
        if (frame.header.encoded_count != 0U ||
            frame.header.total_word_count != kExpectedWords ||
            frame.raw_packet.raw_words.size() !=
                kExpectedWords) {
            return Pm4DrawControlLowerResult::failure(
                lower_error(
                    Pm4DrawControlLowerErrorCode::
                        malformed_num_instances,
                    frame,
                    kExpectedWords));
        }

        return Pm4DrawControlLowerResult::success(
            GraphicsIrEmission{
                .operation =
                    GraphicsIrSetInstanceCount{
                        .instance_count =
                            raw_word_value(
                                frame.raw_packet.raw_words[1]),
                    },
                .provenance =
                    GraphicsIrProvenance{
                        .source_packet =
                            frame.raw_packet,
                    },
            });
    }

    if (frame.header.opcode ==
        kPm4DrawIndexAutoOpcode) {
        constexpr std::size_t kExpectedWords = 3U;
        if (frame.header.encoded_count != 1U ||
            frame.header.total_word_count != kExpectedWords ||
            frame.raw_packet.raw_words.size() !=
                kExpectedWords) {
            return Pm4DrawControlLowerResult::failure(
                lower_error(
                    Pm4DrawControlLowerErrorCode::
                        malformed_draw_index_auto,
                    frame,
                    kExpectedWords));
        }

        return Pm4DrawControlLowerResult::success(
            GraphicsIrEmission{
                .operation =
                    GraphicsIrDrawIndexAuto{
                        .index_count =
                            raw_word_value(
                                frame.raw_packet.raw_words[1]),
                        .initiator =
                            raw_word_value(
                                frame.raw_packet.raw_words[2]),
                    },
                .provenance =
                    GraphicsIrProvenance{
                        .source_packet =
                            frame.raw_packet,
                    },
            });
    }

    return Pm4DrawControlLowerResult::success(
        make_unsupported_packet_ir(
            frame.raw_packet));
}

}  // namespace astraea::graphics
