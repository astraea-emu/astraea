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
        Pm4DrawControlLowerErrorCode::
            malformed_draw_index_auto;
    std::size_t word_offset = 0;
    std::size_t expected_total_word_count = 0;
    std::size_t actual_total_word_count = 0;

    auto operator<=>(const Pm4DrawControlLowerError&) const = default;
};

using Pm4DrawControlLowerResult =
    astraea::core::Result<
        GraphicsIrEmission,
        Pm4DrawControlLowerError>;

// Lowers only the two generic Type-3 draw controls required by Astraea's first
// bounded raster workload:
//
// - NUM_INSTANCES (0x2f): one payload dword;
// - DRAW_INDEX_AUTO (0x2d): index_count + raw initiator.
//
// The DRAW_INDEX_AUTO initiator is intentionally kept opaque here. The first
// observed PS5 path uses raw value 2, but field interpretation belongs in a
// later evidence-bounded draw planner.
//
// Every other opcode remains typed unsupported Graphics IR.
[[nodiscard]] Pm4DrawControlLowerResult
lower_pm4_draw_control_frame_to_graphics_ir(
    const Pm4Type3Frame& frame);

}  // namespace astraea::graphics
