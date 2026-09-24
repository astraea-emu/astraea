#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/color_target_state.hpp>
#include <astraea/graphics/gfx10_surface_layout.hpp>
#include <astraea/graphics/gpu_image.hpp>

namespace astraea::graphics {

// Public GFX10 register/AddrLib values for the bounded color-target subset
// currently representable by Astraea's typed guest-image model.
inline constexpr std::uint8_t kGfx10ColorFormatR8G8B8A8 = 10U;
inline constexpr std::uint8_t kGfx10ColorNumberUnorm = 0U;
inline constexpr std::uint8_t kGfx10ColorSwapStandard = 0U;
inline constexpr std::uint8_t kGfx10ColorSwizzleLinear = 0U;
inline constexpr std::uint8_t kGfx10ResourceType2d = 1U;
inline constexpr std::uint32_t kGfx10R8G8B8A8BytesPerPixel = 4U;

enum class Gfx10ColorTargetImageErrorCode {
    unsupported_endian,
    unsupported_format,
    unsupported_number_type,
    unsupported_component_swap,
    linear_general_not_supported,
    fast_clear_not_supported,
    compression_not_supported,
    fmask_not_supported,
    dcc_not_supported,
    cmask_not_supported,
    nbc_tiling_not_supported,
    unsupported_swizzle_mode,
    unsupported_resource_type,
    logical_size_overflow,
    surface_layout_failure,
    pitch_unrepresentable,
    base_address_misaligned,
};

struct Gfx10ColorTargetImageError {
    Gfx10ColorTargetImageErrorCode code =
        Gfx10ColorTargetImageErrorCode::unsupported_format;
    std::uint64_t actual_value = 0;
    std::optional<Gfx10AlignedLinear2dLayoutError>
        surface_layout_error;

    auto operator<=>(const Gfx10ColorTargetImageError&) const =
        default;
};

using Gfx10ColorTargetImageResult =
    astraea::core::Result<
        GuestGpuImageDescriptor,
        Gfx10ColorTargetImageError>;

// Maps the public GFX10 color-target subset currently supported by Astraea's
// guest image model into a backend-neutral typed image descriptor:
//
// - ENDIAN_NONE;
// - COLOR_8_8_8_8;
// - UNORM;
// - standard component swap;
// - ADDR_SW_LINEAR ordinary aligned-linear layout;
// - 2D resource;
// - no fast-clear/compression/FMASK/DCC/CMASK/NBC metadata behavior.
//
// Width and height are not workload-hardcoded. Logical pixel bytes are derived
// independently from the physical aligned-linear surface extent.
//
// The color write mask is intentionally not part of image storage identity;
// draw/raster policy owns that state separately.
//
// This function does not register backing storage, create Vulkan objects,
// infer PS5-specific surface behavior, or accept tiled/compressed layouts.
[[nodiscard]] Gfx10ColorTargetImageResult
plan_gfx10_linear_rgba8_unorm_color_target_image(
    const ColorTarget0ContextState& target) noexcept;

}  // namespace astraea::graphics
