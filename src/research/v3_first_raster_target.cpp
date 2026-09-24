#include <astraea/research/v3_first_raster_target.hpp>

#include <cstdint>

namespace astraea::research {
namespace {

[[nodiscard]] V3FirstRasterTargetError error(
    V3FirstRasterTargetErrorCode code,
    std::uint64_t actual_value = 0,
    std::optional<
        astraea::graphics::Gfx10AlignedLinear2dLayoutError>
        layout_error = std::nullopt) noexcept {
    return V3FirstRasterTargetError{
        .code = code,
        .actual_value = actual_value,
        .surface_layout_error = layout_error,
    };
}

}  // namespace

V3FirstRasterTargetResult
plan_v3_first_raster_target(
    const astraea::graphics::ColorTarget0ContextState& target) noexcept {
    const auto layout =
        astraea::graphics::
            compute_gfx10_aligned_linear_2d_layout(
                kV3FirstRasterTargetWidth,
                kV3FirstRasterTargetHeight,
                kV3FirstRasterTargetBytesPerPixel);
    if (!layout.has_value()) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    surface_layout_failure,
                0U,
                layout.error()));
    }

    if (target.base_address.value == 0U) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    null_base_address));
    }

    if ((target.base_address.value %
         layout->base_alignment_bytes) != 0U) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    base_address_not_surface_aligned,
                target.base_address.value));
    }

    if (target.write_mask !=
        kV3FirstRasterTargetWriteMask) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    unsupported_write_mask,
                target.write_mask));
    }

    if (target.format !=
        kV3FirstRasterTargetColorFormat) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    unsupported_color_format,
                target.format));
    }

    if (target.number_type !=
        kV3FirstRasterTargetNumberType) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    unsupported_number_type,
                target.number_type));
    }

    if (target.component_swap !=
        kV3FirstRasterTargetComponentSwap) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    unsupported_component_swap,
                target.component_swap));
    }

    if (target.dcc_enabled) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    dcc_not_supported,
                1U));
    }

    if (target.width != kV3FirstRasterTargetWidth ||
        target.height != kV3FirstRasterTargetHeight) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    unsupported_dimensions,
                (static_cast<std::uint64_t>(
                     target.width)
                 << 32U) |
                    static_cast<std::uint64_t>(
                        target.height)));
    }

    if (target.color_sw_mode !=
        kV3FirstRasterTargetColorSwMode) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    unsupported_color_swizzle_mode,
                target.color_sw_mode));
    }

    if (target.resource_type !=
        kV3FirstRasterTargetResourceType) {
        return V3FirstRasterTargetResult::failure(
            error(
                V3FirstRasterTargetErrorCode::
                    unsupported_resource_type,
                target.resource_type));
    }

    const auto logical_byte_count =
        static_cast<std::uint64_t>(
            kV3FirstRasterTargetWidth) *
        static_cast<std::uint64_t>(
            kV3FirstRasterTargetHeight) *
        static_cast<std::uint64_t>(
            kV3FirstRasterTargetBytesPerPixel);

    return V3FirstRasterTargetResult::success(
        V3FirstRasterTargetPlan{
            .base_address = target.base_address,
            .width = target.width,
            .height = target.height,
            .bytes_per_pixel =
                kV3FirstRasterTargetBytesPerPixel,
            .logical_byte_count =
                logical_byte_count,
            .surface_pitch_pixels =
                layout->pitch_pixels,
            .surface_pitch_bytes =
                layout->pitch_bytes,
            .surface_base_alignment_bytes =
                layout->base_alignment_bytes,
            .surface_byte_count =
                layout->surface_byte_count,
            .host_format =
                V3FirstRasterTargetHostFormat::
                    r8g8b8a8_unorm,
        });
}

}  // namespace astraea::research
