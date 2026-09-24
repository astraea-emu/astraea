#pragma once

#include <compare>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/color_target_state.hpp>
#include <astraea/graphics/gfx10_surface_layout.hpp>
#include <astraea/graphics/gpu_allocation_address_space.hpp>
#include <astraea/graphics/gpu_image.hpp>

namespace astraea::research {

inline constexpr std::uint8_t kV3FirstRasterWriteMask = 0x0fU;
inline constexpr std::uint8_t kV3FirstRasterColorFormat = 10U;
inline constexpr std::uint8_t kV3FirstRasterNumberType = 0U;
inline constexpr std::uint8_t kV3FirstRasterComponentSwap = 0U;
inline constexpr std::uint8_t kV3FirstRasterColorSwMode = 0U;
inline constexpr std::uint8_t kV3FirstRasterResourceType = 1U;
inline constexpr std::uint32_t kV3FirstRasterWidth = 4U;
inline constexpr std::uint32_t kV3FirstRasterHeight = 4U;
inline constexpr std::uint32_t kV3FirstRasterBytesPerPixel = 4U;
inline constexpr std::uint64_t kV3FirstRasterLogicalByteCount = 64U;

struct V3FirstRasterTargetPlan {
    astraea::graphics::GuestGpuImageDescriptor image;

    auto operator<=>(const V3FirstRasterTargetPlan&) const = default;
};

enum class V3FirstRasterTargetErrorCode {
    null_base_address,
    unsupported_write_mask,
    unsupported_color_format,
    unsupported_number_type,
    unsupported_component_swap,
    dcc_not_supported,
    unsupported_dimensions,
    unsupported_color_swizzle_mode,
    unsupported_resource_type,
    surface_layout_failure,
    base_address_misaligned,
};

struct V3FirstRasterTargetError {
    V3FirstRasterTargetErrorCode code =
        V3FirstRasterTargetErrorCode::null_base_address;
    std::uint64_t actual_value = 0;
    std::optional<astraea::graphics::Gfx10AlignedLinear2dLayoutError>
        surface_layout_error;

    auto operator<=>(const V3FirstRasterTargetError&) const = default;
};

using V3FirstRasterTargetResult =
    astraea::core::Result<
        V3FirstRasterTargetPlan,
        V3FirstRasterTargetError>;

// Converts only the selected owned 4x4 Color Target 0 profile into a typed
// guest-image descriptor.
//
// All generic register decoding, GFX10 layout arithmetic, allocation identity,
// and image validation live in verified graphics primitives. This provisional
// gate contributes only the exact workload profile:
//
// - RGBA write mask;
// - COLOR_8_8_8_8 / UNORM / standard component swap;
// - DCC disabled;
// - 4x4;
// - ordinary aligned-linear swizzle;
// - 2D resource type.
//
// Physical pitch/backing is derived through the verified GFX10 layout
// calculator rather than hard-coded here.
[[nodiscard]] V3FirstRasterTargetResult
plan_v3_first_raster_target(
    const astraea::graphics::ColorTarget0ContextState& target) noexcept;

enum class V3FirstRasterImageErrorCode {
    image_registration_failure,
};

struct V3FirstRasterImageError {
    V3FirstRasterImageErrorCode code =
        V3FirstRasterImageErrorCode::image_registration_failure;
    astraea::graphics::GuestGpuImageRegistrationError registration_error;

    auto operator<=>(const V3FirstRasterImageError&) const = default;
};

using V3FirstRasterImageResult =
    astraea::core::Result<
        astraea::graphics::GuestGpuImageId,
        V3FirstRasterImageError>;

// Registers the already-planned first raster target over existing guest GPU
// backing. No backing allocation, Vulkan object, draw, or shader execution is
// performed here.
[[nodiscard]] V3FirstRasterImageResult
register_v3_first_raster_image(
    const V3FirstRasterTargetPlan& plan,
    const astraea::graphics::GuestGpuAllocationAddressSpace& allocations,
    astraea::graphics::GuestGpuImageRegistry& images);

}  // namespace astraea::research
