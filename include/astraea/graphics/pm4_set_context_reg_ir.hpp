#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>

namespace astraea::graphics {

inline constexpr std::uint8_t kPm4SetContextRegOpcode = 0x69;
inline constexpr std::size_t kPm4ContextRegisterWindowDwords = 0x400;

enum class Pm4SetContextRegLowerErrorCode {
    malformed_set_context_reg,
    unsupported_register_control_bits,
    register_range_out_of_bounds,
    host_allocation_failure,
};

struct Pm4SetContextRegLowerError {
    Pm4SetContextRegLowerErrorCode code =
        Pm4SetContextRegLowerErrorCode::malformed_set_context_reg;
    std::size_t word_offset = 0;
    std::uint32_t raw_offset_control_word = 0;
    std::size_t value_count = 0;

    auto operator<=>(const Pm4SetContextRegLowerError&) const = default;
};

using Pm4SetContextRegLowerResult =
    astraea::core::Result<
        GraphicsIrEmission,
        Pm4SetContextRegLowerError>;

// Lowers one already-framed generic Type-3 packet.
//
// Only SET_CONTEXT_REG (0x69) receives semantics in this profile. Every other
// opcode remains the existing typed unsupported Graphics IR operation.
// Register offsets remain generic AMD context-register offsets; no PS5-specific
// field meaning is assigned here.
[[nodiscard]] Pm4SetContextRegLowerResult
lower_pm4_set_context_reg_frame_to_graphics_ir(
    const Pm4Type3Frame& frame);

}  // namespace astraea::graphics
