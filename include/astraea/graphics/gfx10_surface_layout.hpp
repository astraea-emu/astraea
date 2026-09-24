#pragma once

#include <compare>
#include <cstdint>

#include <astraea/core/result.hpp>

namespace astraea::graphics {

inline constexpr std::uint32_t
    kGfx10AlignedLinearBaseAlignmentBytes = 256U;

struct Gfx10AlignedLinear2dLayout {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bytes_per_element = 0;

    std::uint32_t pitch_alignment_pixels = 0;
    std::uint32_t pitch_pixels = 0;
    std::uint64_t pitch_bytes = 0;

    std::uint32_t base_alignment_bytes =
        kGfx10AlignedLinearBaseAlignmentBytes;
    std::uint64_t surface_byte_count = 0;

    auto operator<=>(const Gfx10AlignedLinear2dLayout&) const = default;
};

enum class Gfx10AlignedLinear2dLayoutErrorCode {
    zero_dimension,
    zero_element_size,
    unsupported_element_size,
    pitch_alignment_overflow,
    pitch_overflow,
    surface_size_overflow,
};

struct Gfx10AlignedLinear2dLayoutError {
    Gfx10AlignedLinear2dLayoutErrorCode code =
        Gfx10AlignedLinear2dLayoutErrorCode::zero_dimension;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bytes_per_element = 0;

    auto operator<=>(const Gfx10AlignedLinear2dLayoutError&) const =
        default;
};

using Gfx10AlignedLinear2dLayoutResult =
    astraea::core::Result<
        Gfx10AlignedLinear2dLayout,
        Gfx10AlignedLinear2dLayoutError>;

// Computes the public AMD GFX10 ordinary aligned-linear 2D base-level layout
// used by AddrLib's HwlComputeSurfaceInfoLinear:
//
//   pitchAlign = 256 / elementBytes
//   pitch      = align(width, pitchAlign)
//   baseAlign  = 256
//   sliceSize  = pitch * height * elementBytes
//
// This intentionally excludes LINEAR_GENERAL, mip chains, arrays/3D slices,
// custom pitch, block-compressed elements, and metadata surfaces.
[[nodiscard]] Gfx10AlignedLinear2dLayoutResult
compute_gfx10_aligned_linear_2d_layout(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bytes_per_element) noexcept;

}  // namespace astraea::graphics
