#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/color_target_state.hpp>
#include <astraea/graphics/gfx10_surface_layout.hpp>

namespace astraea::research {

// Exact workload profile for Astraea's first synthetic offscreen raster target.
// These are V3 research constraints, not generic PS5 defaults.
inline constexpr std::uint8_t
    kV3FirstRasterTargetColorFormat = 10U;
inline constexpr std::uint8_t
    kV3FirstRasterTargetNumberType = 0U;
inline constexpr std::uint8_t
    kV3FirstRasterTargetComponentSwap = 0U;
inline constexpr std::uint8_t
    kV3FirstRasterTargetColorSwMode = 0U;
inline constexpr std::uint8_t
    kV3FirstRasterTargetResourceType = 1U;
inline constexpr std::uint8_t
    kV3FirstRasterTargetWriteMask = 0x0fU;
inline constexpr std::uint32_t
    kV3FirstRasterTargetWidth = 4U;
inline constexpr std::uint32_t
    kV3FirstRasterTargetHeight = 4U;
inline constexpr std::uint32_t
    kV3FirstRasterTargetBytesPerPixel = 4U;

enum class V3FirstRasterTargetHostFormat {
    r8g8b8a8_unorm,
};

struct V3FirstRasterTargetPlan {
    astraea::graphics::GpuVirtualAddress base_address;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bytes_per_pixel = 0;
    std::uint64_t logical_byte_count = 0;

    std::uint32_t surface_pitch_pixels = 0;
    std::uint64_t surface_pitch_bytes = 0;
    std::uint32_t surface_base_alignment_bytes = 0;
    std::uint64_t surface_byte_count = 0;

    V3FirstRasterTargetHostFormat host_format =
        V3FirstRasterTargetHostFormat::r8g8b8a8_unorm;

    auto operator<=>(const V3FirstRasterTargetPlan&) const = default;
};

enum class V3FirstRasterTargetErrorCode {
    surface_layout_failure,
    null_base_address,
    base_address_not_surface_aligned,
    unsupported_write_mask,
    unsupported_color_format,
    unsupported_number_type,
    unsupported_component_swap,
    dcc_not_supported,
    unsupported_dimensions,
    unsupported_color_swizzle_mode,
    unsupported_resource_type,
};

struct V3FirstRasterTargetError {
    V3FirstRasterTargetErrorCode code =
        V3FirstRasterTargetErrorCode::null_base_address;
    std::uint64_t actual_value = 0;
    std::optional<
        astraea::graphics::Gfx10AlignedLinear2dLayoutError>
        surface_layout_error;

    auto operator<=>(const V3FirstRasterTargetError&) const = default;
};

using V3FirstRasterTargetResult =
    astraea::core::Result<
        V3FirstRasterTargetPlan,
        V3FirstRasterTargetError>;

// Accepts only the bounded synthetic 4x4 RGBA8 UNORM, ordinary aligned-linear
// 2D target. Raw register decoding and GFX10 surface arithmetic live in the
// verified graphics layer; this research gate merely selects one exact
// workload profile.
//
// No guest allocation lookup, image registration, Vulkan materialization,
// tiling conversion, draw execution, or LinkShaders success occurs here.
[[nodiscard]] V3FirstRasterTargetResult
plan_v3_first_raster_target(
    const astraea::graphics::ColorTarget0ContextState& target) noexcept;

}  // namespace astraea::research
