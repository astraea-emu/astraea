#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>

namespace astraea::graphics {

inline constexpr std::uint8_t kPm4SetUconfigRegOpcode = 0x79;
inline constexpr std::size_t
    kPm4UserConfigRegisterWindowDwords = 0x400;

enum class Pm4SetUconfigRegLowerErrorCode {
    malformed_set_uconfig_reg,
    unsupported_register_control_bits,
    register_range_out_of_bounds,
    host_allocation_failure,
};

struct Pm4SetUconfigRegLowerError {
    Pm4SetUconfigRegLowerErrorCode code =
        Pm4SetUconfigRegLowerErrorCode::
            malformed_set_uconfig_reg;
    std::size_t word_offset = 0;
    std::uint32_t raw_offset_control_word = 0;
    std::size_t value_count = 0;

    auto operator<=>(
        const Pm4SetUconfigRegLowerError&) const = default;
};

using Pm4SetUconfigRegLowerResult =
    astraea::core::Result<
        GraphicsIrEmission,
        Pm4SetUconfigRegLowerError>;

// Lowers one already-framed generic Type-3 packet.
//
// SET_UCONFIG_REG is the generic AMD packet used for the 0xc000..0xc3ff
// user-config register window. Astraea stores the packet's relative 0..0x3ff
// register offset exactly, matching the existing SH/context IR convention.
//
// This first bounded profile accepts only an offset/control word whose upper
// 16 bits are zero. Indexed/control variants remain unsupported rather than
// being guessed.
[[nodiscard]] Pm4SetUconfigRegLowerResult
lower_pm4_set_uconfig_reg_frame_to_graphics_ir(
    const Pm4Type3Frame& frame);

}  // namespace astraea::graphics
