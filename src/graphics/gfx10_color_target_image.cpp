#include <astraea/graphics/gfx10_color_target_image.hpp>

#include <cstdint>
#include <limits>
#include <optional>

namespace astraea::graphics {
namespace {

constexpr std::uint32_t kLinearGeneralBit = 1U << 7U;
constexpr std::uint32_t kFastClearBit = 1U << 13U;
constexpr std::uint32_t kCompressionBit = 1U << 14U;
constexpr std::uint32_t kFmaskCompressionDisableBit = 1U << 26U;
constexpr std::uint32_t kFmaskCompressOneFragmentBit = 1U << 27U;
constexpr std::uint32_t kCmaskAddressTypeMask = 0x3U << 29U;
constexpr std::uint32_t kNbcTilingBit = 1U << 31U;

[[nodiscard]] Gfx10ColorTargetImageError error(
    Gfx10ColorTargetImageErrorCode code,
    std::uint64_t actual_value = 0U,
    std::optional<Gfx10AlignedLinear2dLayoutError> layout_error =
        std::nullopt) noexcept {
    return Gfx10ColorTargetImageError{
        .code = code,
        .actual_value = actual_value,
        .surface_layout_error = layout_error,
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

Gfx10ColorTargetImageResult
plan_gfx10_linear_rgba8_unorm_color_target_image(
    const ColorTarget0ContextState& target) noexcept {
    if (target.format != kGfx10ColorFormatR8G8B8A8) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    unsupported_format,
                target.format));
    }

    if (target.number_type != kGfx10ColorNumberUnorm) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    unsupported_number_type,
                target.number_type));
    }

    if (target.component_swap != kGfx10ColorSwapStandard) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    unsupported_component_swap,
                target.component_swap));
    }

    if ((target.raw_info & kLinearGeneralBit) != 0U) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    linear_general_not_supported,
                target.raw_info));
    }

    if ((target.raw_info & kFastClearBit) != 0U) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    fast_clear_not_supported,
                target.raw_info));
    }

    if ((target.raw_info & kCompressionBit) != 0U) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    compression_not_supported,
                target.raw_info));
    }

    if ((target.raw_info &
         (kFmaskCompressionDisableBit |
          kFmaskCompressOneFragmentBit)) != 0U) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    fmask_not_supported,
                target.raw_info));
    }

    if (target.dcc_enabled) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    dcc_not_supported,
                target.raw_info));
    }

    if ((target.raw_info & kCmaskAddressTypeMask) != 0U) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    cmask_not_supported,
                target.raw_info));
    }

    if ((target.raw_info & kNbcTilingBit) != 0U) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    nbc_tiling_not_supported,
                target.raw_info));
    }

    if (target.color_sw_mode != kGfx10ColorSwizzleLinear) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    unsupported_swizzle_mode,
                target.color_sw_mode));
    }

    if (target.resource_type != kGfx10ResourceType2d) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    unsupported_resource_type,
                target.resource_type));
    }

    std::uint64_t logical_pixels = 0U;
    if (!checked_multiply(
            target.width,
            target.height,
            logical_pixels)) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    logical_size_overflow));
    }

    std::uint64_t logical_bytes = 0U;
    if (!checked_multiply(
            logical_pixels,
            kGfx10R8G8B8A8BytesPerPixel,
            logical_bytes)) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    logical_size_overflow));
    }

    const auto layout =
        compute_gfx10_aligned_linear_2d_layout(
            target.width,
            target.height,
            kGfx10R8G8B8A8BytesPerPixel);
    if (!layout.has_value()) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    surface_layout_failure,
                0U,
                layout.error()));
    }

    if (layout->pitch_bytes >
        std::numeric_limits<std::uint32_t>::max()) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    pitch_unrepresentable,
                layout->pitch_bytes));
    }

    if ((target.base_address.value %
         layout->base_alignment_bytes) != 0U) {
        return Gfx10ColorTargetImageResult::failure(
            error(
                Gfx10ColorTargetImageErrorCode::
                    base_address_misaligned,
                target.base_address.value));
    }

    return Gfx10ColorTargetImageResult::success(
        GuestGpuImageDescriptor{
            .base_address = target.base_address,
            .format =
                GuestGpuImageFormat::
                    r8g8b8a8_unorm,
            .width = target.width,
            .height = target.height,
            .bytes_per_pixel =
                kGfx10R8G8B8A8BytesPerPixel,
            .logical_byte_count = logical_bytes,
            .layout =
                GuestGpuSurfaceLayout{
                    .kind =
                        GuestGpuSurfaceLayoutKind::
                            gfx10_aligned_linear,
                    .pitch_pixels = layout->pitch_pixels,
                    .pitch_bytes =
                        static_cast<std::uint32_t>(
                            layout->pitch_bytes),
                    .base_alignment_bytes =
                        layout->base_alignment_bytes,
                    .surface_byte_count =
                        layout->surface_byte_count,
                },
        });
}

}  // namespace astraea::graphics
