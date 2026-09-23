#include <astraea/graphics/pm4_write_data_ir.hpp>

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

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

[[nodiscard]] Pm4WriteDataLowerError lower_error(
    Pm4WriteDataLowerErrorCode code,
    const Pm4Type3Frame& frame,
    std::uint32_t raw_control_word = 0,
    GpuVirtualAddress destination = {},
    std::size_t value_count = 0) noexcept {
    return Pm4WriteDataLowerError{
        .code = code,
        .word_offset = frame.word_offset,
        .type3_header_control_bits =
            frame.header.low_control_bits,
        .raw_control_word = raw_control_word,
        .destination = destination,
        .value_count = value_count,
    };
}

}  // namespace

Pm4WriteDataLowerResult
lower_pm4_write_data_frame_to_graphics_ir(
    const Pm4Type3Frame& frame) {
    try {
        if (frame.header.opcode !=
            kPm4WriteDataOpcode) {
            return Pm4WriteDataLowerResult::success(
                make_unsupported_packet_ir(
                    frame.raw_packet));
        }

        if (frame.header.low_control_bits != 0U) {
            return Pm4WriteDataLowerResult::failure(
                lower_error(
                    Pm4WriteDataLowerErrorCode::
                        unsupported_type3_header_control_bits,
                    frame));
        }

        const auto encoded_count =
            static_cast<std::size_t>(
                frame.header.encoded_count);
        if (encoded_count < 3U ||
            frame.raw_packet.raw_words.size() !=
                encoded_count + 2U ||
            frame.header.total_word_count !=
                encoded_count + 2U) {
            return Pm4WriteDataLowerResult::failure(
                lower_error(
                    Pm4WriteDataLowerErrorCode::
                        malformed_write_data,
                    frame));
        }

        const auto value_count =
            encoded_count - 2U;
        if (value_count == 0U) {
            return Pm4WriteDataLowerResult::failure(
                lower_error(
                    Pm4WriteDataLowerErrorCode::
                        malformed_write_data,
                    frame,
                    0U,
                    {},
                    value_count));
        }

        const auto control_word =
            raw_word_value(
                frame.raw_packet.raw_words[1]);
        if (control_word !=
            kPm4WriteDataSupportedControlWord) {
            return Pm4WriteDataLowerResult::failure(
                lower_error(
                    Pm4WriteDataLowerErrorCode::
                        unsupported_write_control,
                    frame,
                    control_word,
                    {},
                    value_count));
        }

        const auto address_lo =
            raw_word_value(
                frame.raw_packet.raw_words[2]);
        const auto address_hi =
            raw_word_value(
                frame.raw_packet.raw_words[3]);
        const auto destination =
            GpuVirtualAddress{
                .value =
                    static_cast<std::uint64_t>(
                        address_lo) |
                    (static_cast<std::uint64_t>(
                         address_hi)
                     << 32U),
            };

        if ((destination.value & 0x3ULL) != 0U) {
            return Pm4WriteDataLowerResult::failure(
                lower_error(
                    Pm4WriteDataLowerErrorCode::
                        destination_address_unaligned,
                    frame,
                    control_word,
                    destination,
                    value_count));
        }

        std::vector<std::uint32_t> values;
        values.reserve(value_count);
        for (std::size_t index = 0;
             index < value_count;
             ++index) {
            values.push_back(
                raw_word_value(
                    frame.raw_packet.raw_words[
                        index + 4U]));
        }

        return Pm4WriteDataLowerResult::success(
            GraphicsIrEmission{
                .operation =
                    GraphicsIrGpuMemoryWrite{
                        .destination = destination,
                        .values = std::move(values),
                    },
                .provenance =
                    GraphicsIrProvenance{
                        .source_packet =
                            frame.raw_packet,
                    },
            });
    } catch (const std::bad_alloc&) {
        return Pm4WriteDataLowerResult::failure(
            lower_error(
                Pm4WriteDataLowerErrorCode::
                    host_allocation_failure,
                frame));
    } catch (const std::length_error&) {
        return Pm4WriteDataLowerResult::failure(
            lower_error(
                Pm4WriteDataLowerErrorCode::
                    host_allocation_failure,
                frame));
    }
}

}  // namespace astraea::graphics
