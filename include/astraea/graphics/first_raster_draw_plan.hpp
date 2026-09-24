#pragma once

#include <compare>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/user_config_register_state.hpp>

namespace astraea::graphics {

// Exact raw values for the first bounded owned raster profile.
//
// These constants deliberately describe one accepted workload shape. Raw
// primitive value 4 and raw initiator 2 are not promoted here into general
// topology or draw-initiator semantics.
inline constexpr std::uint16_t
    kFirstRasterPrimitiveTypeUconfigOffset = 0x242U;
inline constexpr std::uint32_t
    kFirstRasterPrimitiveTypeRaw = 4U;
inline constexpr std::uint32_t
    kFirstRasterInstanceCount = 1U;
inline constexpr std::uint32_t
    kFirstRasterIndexCount = 3U;
inline constexpr std::uint32_t
    kFirstRasterAutoIndexInitiatorRaw = 2U;

struct FirstRasterDrawPlan {
    std::uint32_t primitive_type_raw = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t initiator_raw = 0;

    auto operator<=>(const FirstRasterDrawPlan&) const = default;
};

enum class FirstRasterDrawPlanErrorCode {
    primitive_type_not_initialized,
    unsupported_primitive_type,
    unsupported_instance_count,
    unsupported_index_count,
    unsupported_initiator,
};

struct FirstRasterDrawPlanError {
    FirstRasterDrawPlanErrorCode code =
        FirstRasterDrawPlanErrorCode::
            primitive_type_not_initialized;
    std::uint32_t actual_value = 0;

    auto operator<=>(const FirstRasterDrawPlanError&) const = default;
};

using FirstRasterDrawPlanResult =
    astraea::core::Result<
        FirstRasterDrawPlan,
        FirstRasterDrawPlanError>;

// Pure acceptance gate for Astraea's first owned raster draw.
//
// Generic PM4 lowering remains generic. This function is the explicit narrow
// consumer that accepts only the exact first-profile raw state.
[[nodiscard]] FirstRasterDrawPlanResult
plan_first_raster_draw(
    const UserConfigRegisterState& user_config_state,
    const GraphicsIrSetInstanceCount& instances,
    const GraphicsIrDrawIndexAuto& draw) noexcept;

}  // namespace astraea::graphics
