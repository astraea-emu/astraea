#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>

namespace astraea::graphics {

inline constexpr std::uint8_t kPm4SetShRegOpcode = 0x76;
inline constexpr std::size_t kPm4ShaderRegisterWindowDwords = 0x400;

enum class Pm4SetShRegLowerErrorCode {
    malformed_set_sh_reg,
    unsupported_register_control_bits,
    register_range_out_of_bounds,
    host_allocation_failure,
};

struct Pm4SetShRegLowerError {
    Pm4SetShRegLowerErrorCode code =
        Pm4SetShRegLowerErrorCode::malformed_set_sh_reg;
    std::size_t word_offset = 0;
    std::uint32_t raw_offset_control_word = 0;
    std::size_t value_count = 0;

    auto operator<=>(const Pm4SetShRegLowerError&) const = default;
};

using Pm4SetShRegLowerResult =
    astraea::core::Result<
        GraphicsIrEmission,
        Pm4SetShRegLowerError>;

// Lowers one already-framed generic Type-3 packet.
//
// Only SET_SH_REG (0x76) receives semantics in this profile. Every other
// opcode remains the existing typed unsupported Graphics IR operation.
[[nodiscard]] Pm4SetShRegLowerResult
lower_pm4_type3_frame_to_graphics_ir(
    const Pm4Type3Frame& frame);

}  // namespace astraea::graphics
