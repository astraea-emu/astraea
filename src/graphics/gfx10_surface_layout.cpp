#include <astraea/graphics/gfx10_surface_layout.hpp>

#include <cstdint>
#include <limits>

namespace astraea::graphics {
namespace {

[[nodiscard]] Gfx10AlignedLinear2dLayoutError error(
    Gfx10AlignedLinear2dLayoutErrorCode code,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bytes_per_element) noexcept {
    return Gfx10AlignedLinear2dLayoutError{
        .code = code,
        .width = width,
        .height = height,
        .bytes_per_element = bytes_per_element,
    };
}

[[nodiscard]] bool checked_multiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& output) noexcept {
    if (left != 0U &&
        right >
            std::numeric_limits<std::uint64_t>::max() /
                left) {
        return false;
    }

    output = left * right;
    return true;
}

}  // namespace

Gfx10AlignedLinear2dLayoutResult
compute_gfx10_aligned_linear_2d_layout(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bytes_per_element) noexcept {
    if (width == 0U || height == 0U) {
        return Gfx10AlignedLinear2dLayoutResult::failure(
            error(
                Gfx10AlignedLinear2dLayoutErrorCode::
                    zero_dimension,
                width,
                height,
                bytes_per_element));
    }

    if (bytes_per_element == 0U) {
        return Gfx10AlignedLinear2dLayoutResult::failure(
            error(
                Gfx10AlignedLinear2dLayoutErrorCode::
                    zero_element_size,
                width,
                height,
                bytes_per_element));
    }

    if (bytes_per_element >
            kGfx10AlignedLinearBaseAlignmentBytes ||
        (kGfx10AlignedLinearBaseAlignmentBytes %
         bytes_per_element) != 0U) {
        return Gfx10AlignedLinear2dLayoutResult::failure(
            error(
                Gfx10AlignedLinear2dLayoutErrorCode::
                    unsupported_element_size,
                width,
                height,
                bytes_per_element));
    }

    const auto pitch_alignment =
        kGfx10AlignedLinearBaseAlignmentBytes /
        bytes_per_element;
    if (pitch_alignment == 0U) {
        return Gfx10AlignedLinear2dLayoutResult::failure(
            error(
                Gfx10AlignedLinear2dLayoutErrorCode::
                    pitch_alignment_overflow,
                width,
                height,
                bytes_per_element));
    }

    const auto remainder = width % pitch_alignment;
    const auto padding =
        remainder == 0U
            ? 0U
            : pitch_alignment - remainder;

    if (width >
        std::numeric_limits<std::uint32_t>::max() -
            padding) {
        return Gfx10AlignedLinear2dLayoutResult::failure(
            error(
                Gfx10AlignedLinear2dLayoutErrorCode::
                    pitch_overflow,
                width,
                height,
                bytes_per_element));
    }

    const auto pitch_pixels = width + padding;

    std::uint64_t pitch_bytes = 0;
    if (!checked_multiply(
            pitch_pixels,
            bytes_per_element,
            pitch_bytes)) {
        return Gfx10AlignedLinear2dLayoutResult::failure(
            error(
                Gfx10AlignedLinear2dLayoutErrorCode::
                    pitch_overflow,
                width,
                height,
                bytes_per_element));
    }

    std::uint64_t surface_bytes = 0;
    if (!checked_multiply(
            pitch_bytes,
            height,
            surface_bytes)) {
        return Gfx10AlignedLinear2dLayoutResult::failure(
            error(
                Gfx10AlignedLinear2dLayoutErrorCode::
                    surface_size_overflow,
                width,
                height,
                bytes_per_element));
    }

    return Gfx10AlignedLinear2dLayoutResult::success(
        Gfx10AlignedLinear2dLayout{
            .width = width,
            .height = height,
            .bytes_per_element = bytes_per_element,
            .pitch_alignment_pixels = pitch_alignment,
            .pitch_pixels = pitch_pixels,
            .pitch_bytes = pitch_bytes,
            .base_alignment_bytes =
                kGfx10AlignedLinearBaseAlignmentBytes,
            .surface_byte_count = surface_bytes,
        });
}

}  // namespace astraea::graphics
