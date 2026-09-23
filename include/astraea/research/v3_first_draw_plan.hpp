#pragma once

#include <compare>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/user_config_register_state.hpp>

namespace astraea::research {

// Exact raw values from the first bounded PS5 raster profile. These constants
// describe the accepted experiment shape; they do not generalize every PS5
// draw or every AMD packet field.
inline constexpr std::uint16_t
    kV3FirstDrawPrimitiveTypeUconfigOffset = 0x242U;
inline constexpr std::uint32_t
    kV3FirstDrawTriangleListPrimitiveType = 4U;
inline constexpr std::uint32_t
    kV3FirstDrawInstanceCount = 1U;
inline constexpr std::uint32_t
    kV3FirstDrawIndexCount = 3U;
inline constexpr std::uint32_t
    kV3FirstDrawAutoIndexInitiator = 2U;

struct V3FirstDrawPlan {
    std::uint32_t primitive_type = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t initiator = 0;

    auto operator<=>(const V3FirstDrawPlan&) const = default;
};

enum class V3FirstDrawPlanErrorCode {
    primitive_type_not_initialized,
    unsupported_primitive_type,
    unsupported_instance_count,
    unsupported_index_count,
    unsupported_initiator,
};

struct V3FirstDrawPlanError {
    V3FirstDrawPlanErrorCode code =
        V3FirstDrawPlanErrorCode::
            primitive_type_not_initialized;
    std::uint32_t actual_value = 0;

    auto operator<=>(const V3FirstDrawPlanError&) const = default;
};

using V3FirstDrawPlanResult =
    astraea::core::Result<
        V3FirstDrawPlan,
        V3FirstDrawPlanError>;

// Pure host-side acceptance gate for the first synthetic V3 raster draw.
//
// It consumes:
// - already-applied generic UCONFIG state;
// - one already-lowered NUM_INSTANCES operation;
// - one already-lowered DRAW_INDEX_AUTO operation.
//
// The first profile deliberately requires the exact native-observed primitive
// and draw shape: primitive 4, one instance, three auto-indexed vertices, raw
// initiator 2. It performs no guest access, HLE dispatch, shader execution, or
// Vulkan work.
[[nodiscard]] V3FirstDrawPlanResult
plan_v3_first_draw(
    const astraea::graphics::UserConfigRegisterState& user_config_state,
    const astraea::graphics::GraphicsIrSetInstanceCount& instances,
    const astraea::graphics::GraphicsIrDrawIndexAuto& draw) noexcept;

}  // namespace astraea::research
