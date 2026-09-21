#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::graphics {

inline constexpr std::uint8_t kPm4Type3 = 3;
inline constexpr std::uint8_t kPm4GenericNopOpcode = 0x10;

enum class Pm4PacketKind {
    generic_nop,
    unknown,
};

enum class Pm4ParseErrorCode {
    unsupported_packet_type,
    truncated_packet,
    host_allocation_failure,
};

struct Pm4ParseError {
    Pm4ParseErrorCode code = Pm4ParseErrorCode::host_allocation_failure;
    std::size_t word_index = 0;
    std::uint8_t header_type = 0;
    std::size_t required_words = 0;
    std::size_t available_words = 0;

    auto operator<=>(const Pm4ParseError&) const = default;
};

struct Pm4Type3Header {
    std::uint32_t raw_word = 0;
    std::uint8_t type = 0;
    std::uint8_t opcode = 0;
    std::uint16_t encoded_count = 0;
    std::uint8_t reserved_low = 0;
    std::size_t body_word_count = 0;
    std::size_t total_word_count = 0;

    auto operator<=>(const Pm4Type3Header&) const = default;
};

struct Pm4Packet {
    std::size_t word_index = 0;
    Pm4PacketKind kind = Pm4PacketKind::unknown;
    Pm4Type3Header header;
    std::vector<std::uint32_t> raw_words;

    auto operator<=>(const Pm4Packet&) const = default;
};

struct Pm4CommandBuffer {
    std::vector<Pm4Packet> packets;

    auto operator<=>(const Pm4CommandBuffer&) const = default;
};

using Pm4ParseResult =
    astraea::core::Result<Pm4CommandBuffer, Pm4ParseError>;

[[nodiscard]] Pm4ParseResult parse_generic_pm4_type3(
    std::span<const std::uint32_t> words);

}  // namespace astraea::graphics
