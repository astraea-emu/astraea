#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_address.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>

namespace astraea::graphics {

inline constexpr std::uint8_t kPm4WriteDataOpcode = 0x37U;

// Evidence-bounded first memory profile:
// - dst_sel = 5 (direct memory)
// - address increment = 0
// - write confirm = 1
// - cache policy = 0 (LRU)
// - engine select = 0 (ME)
// - all other control bits = 0
inline constexpr std::uint32_t kPm4WriteDataSupportedControlWord =
    0x00100500U;

enum class Pm4WriteDataLowerErrorCode {
    malformed_write_data,
    unsupported_type3_header_control_bits,
    unsupported_write_control,
    destination_address_unaligned,
    host_allocation_failure,
};

struct Pm4WriteDataLowerError {
    Pm4WriteDataLowerErrorCode code =
        Pm4WriteDataLowerErrorCode::malformed_write_data;
    std::size_t word_offset = 0;
    std::uint8_t type3_header_control_bits = 0;
    std::uint32_t raw_control_word = 0;
    GpuVirtualAddress destination;
    std::size_t value_count = 0;

    auto operator<=>(const Pm4WriteDataLowerError&) const = default;
};

using Pm4WriteDataLowerResult =
    astraea::core::Result<
        GraphicsIrEmission,
        Pm4WriteDataLowerError>;

// Lowers one already-framed Type-3 packet.
//
// Only the evidence-bounded WRITE_DATA memory profile receives semantics.
// Every other opcode remains the existing typed unsupported Graphics IR.
// This function never resolves or mutates guest GPU memory.
[[nodiscard]] Pm4WriteDataLowerResult
lower_pm4_write_data_frame_to_graphics_ir(
    const Pm4Type3Frame& frame);

}  // namespace astraea::graphics
