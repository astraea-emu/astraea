#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::graphics {

inline constexpr std::size_t kPacketWordBytes = 4;

enum class PacketKind {
    unknown,
};

enum class PacketErrorCode {
    command_buffer_not_word_aligned,
    empty_packet,
    packet_range_overflow,
    packet_range_out_of_bounds,
    host_allocation_failure,
};

struct PacketError {
    PacketErrorCode code = PacketErrorCode::host_allocation_failure;
    std::size_t word_offset = 0;
    std::size_t word_count = 0;

    auto operator<=>(const PacketError&) const = default;
};

struct RawPacketWord {
    std::array<std::byte, kPacketWordBytes> bytes{};

    auto operator<=>(const RawPacketWord&) const = default;
};

struct PacketExtent {
    std::size_t word_offset = 0;
    std::size_t word_count = 0;

    auto operator<=>(const PacketExtent&) const = default;
};

struct PacketHeader {
    std::size_t word_offset = 0;
    RawPacketWord raw;

    auto operator<=>(const PacketHeader&) const = default;
};

struct RawPacket {
    PacketExtent extent;
    PacketHeader header;
    PacketKind kind = PacketKind::unknown;
    std::vector<RawPacketWord> raw_words;

    auto operator<=>(const RawPacket&) const = default;
};

using PacketParseResult =
    astraea::core::Result<RawPacket, PacketError>;

// #9 does not establish PS5 packet framing or header bitfields. The caller
// therefore supplies the packet extent; this function only validates bounds,
// preserves raw 32-bit words, and records the first word as an uninterpreted
// header. Packet kind remains unknown until evidence justifies classification.
[[nodiscard]] PacketParseResult parse_raw_packet(
    std::span<const std::byte> command_buffer,
    PacketExtent extent);

}  // namespace astraea::graphics
