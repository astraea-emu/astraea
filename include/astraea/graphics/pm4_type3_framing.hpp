#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/packet.hpp>

namespace astraea::graphics {

inline constexpr std::uint8_t kPm4Type3PacketType = 3;

struct Pm4Type3Header {
    std::uint32_t raw_word = 0;
    std::uint8_t opcode = 0;
    std::uint16_t encoded_count = 0;
    std::uint8_t low_control_bits = 0;
    std::size_t body_word_count = 0;
    std::size_t total_word_count = 0;

    auto operator<=>(const Pm4Type3Header&) const = default;
};

struct Pm4Type3Frame {
    std::size_t word_offset = 0;
    Pm4Type3Header header;
    RawPacket raw_packet;

    auto operator<=>(const Pm4Type3Frame&) const = default;
};

struct Pm4Type3Stream {
    std::vector<Pm4Type3Frame> frames;

    auto operator<=>(const Pm4Type3Stream&) const = default;
};

enum class Pm4Type3FrameErrorCode {
    command_buffer_not_word_aligned,
    unsupported_packet_type,
    truncated_packet,
    host_size_unrepresentable,
    raw_packet_failure,
    host_allocation_failure,
};

struct Pm4Type3FrameError {
    Pm4Type3FrameErrorCode code =
        Pm4Type3FrameErrorCode::host_allocation_failure;
    std::size_t word_offset = 0;
    std::optional<std::uint8_t> header_type;
    std::size_t required_words = 0;
    std::size_t available_words = 0;
    std::optional<PacketError> packet_error;

    auto operator<=>(const Pm4Type3FrameError&) const = default;
};

using Pm4Type3FrameResult =
    astraea::core::Result<
        Pm4Type3Stream,
        Pm4Type3FrameError>;

[[nodiscard]] Pm4Type3FrameResult
frame_pm4_type3_stream(
    std::span<const std::byte> command_buffer);

}  // namespace astraea::graphics
