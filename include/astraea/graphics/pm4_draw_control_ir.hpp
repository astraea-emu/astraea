#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>

namespace astraea::graphics {

inline constexpr std::uint8_t kPm4DrawIndexAutoOpcode = 0x2d;
inline constexpr std::uint8_t kPm4NumInstancesOpcode = 0x2f;

enum class Pm4DrawControlLowerErrorCode {
    malformed_num_instances,
    malformed_draw_index_auto,
};

struct Pm4DrawControlLowerError {
    Pm4DrawControlLowerErrorCode code =
        Pm4DrawControlLowerErrorCode::malformed_draw_index_auto;
    std::size_t word_offset = 0;
    std::size_t expected_total_word_count = 0;
    std::size_t actual_total_word_count = 0;

    auto operator<=>(const Pm4DrawControlLowerError&) const = default;
};

using Pm4DrawControlLowerResult =
    astraea::core::Result<
        GraphicsIrEmission,
        Pm4DrawControlLowerError>;

// Generic lowering for NUM_INSTANCES and DRAW_INDEX_AUTO. The draw initiator
// is retained as a raw value; field interpretation belongs to a later
// evidence-bounded consumer.
[[nodiscard]] Pm4DrawControlLowerResult
lower_pm4_draw_control_frame_to_graphics_ir(
    const Pm4Type3Frame& frame);

}  // namespace astraea::graphics
