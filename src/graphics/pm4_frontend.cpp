#include <astraea/graphics/pm4_frontend.hpp>

#include <new>
#include <stdexcept>
#include <utility>

namespace astraea::graphics {
namespace {

// Generic AMD PM4 Type-3 structural layout only.
//
// Evidence source:
// Linux KFD, commit f0100363d8c374bd8e9ea7c9ba02744f0b802ca4,
// drivers/gpu/drm/amd/amdkfd/kfd_pm4_headers.h
//
// That public AMD-authored header defines bits 31:30 as packet type,
// bits 29:16 as count, bits 15:8 as opcode, and bits 7:0 as reserved.
// It defines count as "number of DWORDs - 1 in the information body".
//
// This parser does not claim that PS5 accepts any particular PM4 packet,
// opcode, register, or desktop-Radeon behavior.

constexpr std::uint32_t kTypeMask = 0x3U;
constexpr unsigned int kTypeShift = 30U;
constexpr std::uint32_t kCountMask = 0x3fffU;
constexpr unsigned int kCountShift = 16U;
constexpr std::uint32_t kOpcodeMask = 0xffU;
constexpr unsigned int kOpcodeShift = 8U;
constexpr std::uint32_t kReservedLowMask = 0xffU;

[[nodiscard]] Pm4ParseError parse_error(
    Pm4ParseErrorCode code,
    std::size_t word_index,
    std::uint8_t header_type,
    std::size_t required_words,
    std::size_t available_words) noexcept {
    return Pm4ParseError{
        .code = code,
        .word_index = word_index,
        .header_type = header_type,
        .required_words = required_words,
        .available_words = available_words,
    };
}

[[nodiscard]] Pm4PacketKind classify_opcode(
    std::uint8_t opcode) noexcept {
    if (opcode == kPm4GenericNopOpcode) {
        return Pm4PacketKind::generic_nop;
    }
    return Pm4PacketKind::unknown;
}

}  // namespace

Pm4ParseResult parse_generic_pm4_type3(
    std::span<const std::uint32_t> words) {
    std::size_t word_index = 0;

    try {
        Pm4CommandBuffer command_buffer;

        while (word_index < words.size()) {
            const std::uint32_t raw_header = words[word_index];
            const auto header_type = static_cast<std::uint8_t>(
                (raw_header >> kTypeShift) & kTypeMask);
            const auto available_words = words.size() - word_index;

            if (header_type != kPm4Type3) {
                return Pm4ParseResult::failure(
                    parse_error(
                        Pm4ParseErrorCode::unsupported_packet_type,
                        word_index,
                        header_type,
                        1,
                        available_words));
            }

            const auto encoded_count = static_cast<std::uint16_t>(
                (raw_header >> kCountShift) & kCountMask);
            const auto opcode = static_cast<std::uint8_t>(
                (raw_header >> kOpcodeShift) & kOpcodeMask);
            const auto reserved_low = static_cast<std::uint8_t>(
                raw_header & kReservedLowMask);

            const auto body_word_count =
                static_cast<std::size_t>(encoded_count) + 1U;
            const auto total_word_count = body_word_count + 1U;

            if (available_words < total_word_count) {
                return Pm4ParseResult::failure(
                    parse_error(
                        Pm4ParseErrorCode::truncated_packet,
                        word_index,
                        header_type,
                        total_word_count,
                        available_words));
            }

            const auto packet_words =
                words.subspan(
                    word_index,
                    total_word_count);

            Pm4Packet packet{
                .word_index = word_index,
                .kind = classify_opcode(opcode),
                .header =
                    Pm4Type3Header{
                        .raw_word = raw_header,
                        .type = header_type,
                        .opcode = opcode,
                        .encoded_count = encoded_count,
                        .reserved_low = reserved_low,
                        .body_word_count = body_word_count,
                        .total_word_count = total_word_count,
                    },
                .raw_words =
                    std::vector<std::uint32_t>(
                        packet_words.begin(),
                        packet_words.end()),
            };

            command_buffer.packets.push_back(
                std::move(packet));
            word_index += total_word_count;
        }

        return Pm4ParseResult::success(
            std::move(command_buffer));
    } catch (const std::bad_alloc&) {
        return Pm4ParseResult::failure(
            parse_error(
                Pm4ParseErrorCode::host_allocation_failure,
                word_index,
                0,
                0,
                0));
    } catch (const std::length_error&) {
        return Pm4ParseResult::failure(
            parse_error(
                Pm4ParseErrorCode::host_allocation_failure,
                word_index,
                0,
                0,
                0));
    }
}

}  // namespace astraea::graphics
