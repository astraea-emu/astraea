#include <astraea/graphics/pm4_set_sh_reg_ir.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace astraea::graphics {
namespace {

[[nodiscard]] std::uint32_t raw_word_value(
    const RawWord& word) noexcept {
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

[[nodiscard]] Pm4SetShRegLowerError lower_error(
    Pm4SetShRegLowerErrorCode code,
    const Pm4Type3Frame& frame,
    std::uint32_t raw_offset_control_word = 0,
    std::size_t value_count = 0) noexcept {
    return Pm4SetShRegLowerError{
        .code = code,
        .word_offset = frame.word_offset,
        .raw_offset_control_word =
            raw_offset_control_word,
        .value_count = value_count,
    };
}

}  // namespace

Pm4SetShRegLowerResult
lower_pm4_type3_frame_to_graphics_ir(
    const Pm4Type3Frame& frame) {
    try {
        if (frame.header.opcode !=
            kPm4SetShRegOpcode) {
            return Pm4SetShRegLowerResult::success(
                make_unsupported_packet_ir(
                    frame.raw_packet));
        }

        const auto value_count =
            static_cast<std::size_t>(
                frame.header.encoded_count);

        if (value_count == 0U ||
            frame.raw_packet.raw_words.size() !=
                value_count + 2U ||
            frame.header.total_word_count !=
                value_count + 2U) {
            return Pm4SetShRegLowerResult::failure(
                lower_error(
                    Pm4SetShRegLowerErrorCode::
                        malformed_set_sh_reg,
                    frame,
                    0U,
                    value_count));
        }

        const auto offset_control_word =
            raw_word_value(
                frame.raw_packet.raw_words[1]);
        const auto control_bits =
            offset_control_word & 0xffff0000U;
        if (control_bits != 0U) {
            return Pm4SetShRegLowerResult::failure(
                lower_error(
                    Pm4SetShRegLowerErrorCode::
                        unsupported_register_control_bits,
                    frame,
                    offset_control_word,
                    value_count));
        }

        const auto start_offset =
            static_cast<std::uint16_t>(
                offset_control_word & 0xffffU);
        const auto start =
            static_cast<std::size_t>(
                start_offset);

        if (start >=
                kPm4ShaderRegisterWindowDwords ||
            value_count >
                kPm4ShaderRegisterWindowDwords -
                    start) {
            return Pm4SetShRegLowerResult::failure(
                lower_error(
                    Pm4SetShRegLowerErrorCode::
                        register_range_out_of_bounds,
                    frame,
                    offset_control_word,
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
                        index + 2U]));
        }

        return Pm4SetShRegLowerResult::success(
            GraphicsIrEmission{
                .operation =
                    GraphicsIrShaderRegisterWriteRange{
                        .start_offset =
                            start_offset,
                        .values =
                            std::move(values),
                    },
                .provenance =
                    GraphicsIrProvenance{
                        .source_packet =
                            frame.raw_packet,
                    },
            });
    } catch (const std::bad_alloc&) {
        return Pm4SetShRegLowerResult::failure(
            lower_error(
                Pm4SetShRegLowerErrorCode::
                    host_allocation_failure,
                frame));
    } catch (const std::length_error&) {
        return Pm4SetShRegLowerResult::failure(
            lower_error(
                Pm4SetShRegLowerErrorCode::
                    host_allocation_failure,
                frame));
    }
}

}  // namespace astraea::graphics
