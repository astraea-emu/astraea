#include <astraea/research/v3_first_raster_binding.hpp>

#include <cstdint>
#include <limits>
#include <utility>

namespace astraea::research {
namespace {

[[nodiscard]] V3FirstRasterTargetError target_error(
    V3FirstRasterTargetErrorCode code,
    std::uint64_t actual_value = 0,
    std::optional<astraea::graphics::Gfx10AlignedLinear2dLayoutError>
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
    if (target.base_address.value == 0U) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    null_base_address));
    }

    if (target.write_mask != kV3FirstRasterWriteMask) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    unsupported_write_mask,
                target.write_mask));
    }

    if (target.format != kV3FirstRasterColorFormat) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    unsupported_color_format,
                target.format));
    }

    if (target.number_type != kV3FirstRasterNumberType) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    unsupported_number_type,
                target.number_type));
    }

    if (target.component_swap != kV3FirstRasterComponentSwap) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    unsupported_component_swap,
                target.component_swap));
    }

    if (target.dcc_enabled) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    dcc_not_supported,
                1U));
    }

    if (target.width != kV3FirstRasterWidth ||
        target.height != kV3FirstRasterHeight) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    unsupported_dimensions,
                (static_cast<std::uint64_t>(target.width) << 32U) |
                    static_cast<std::uint64_t>(target.height)));
    }

    if (target.color_sw_mode != kV3FirstRasterColorSwMode) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    unsupported_color_swizzle_mode,
                target.color_sw_mode));
    }

    if (target.resource_type != kV3FirstRasterResourceType) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    unsupported_resource_type,
                target.resource_type));
    }

    const auto layout =
        astraea::graphics::compute_gfx10_aligned_linear_2d_layout(
            target.width,
            target.height,
            kV3FirstRasterBytesPerPixel);
    if (!layout.has_value()) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    surface_layout_failure,
                0U,
                layout.error()));
    }

    if ((target.base_address.value %
         layout->base_alignment_bytes) != 0U) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    base_address_misaligned,
                target.base_address.value));
    }

    if (layout->pitch_bytes >
        std::numeric_limits<std::uint32_t>::max()) {
        return V3FirstRasterTargetResult::failure(
            target_error(
                V3FirstRasterTargetErrorCode::
                    surface_layout_failure));
    }

    return V3FirstRasterTargetResult::success(
        V3FirstRasterTargetPlan{
            .image =
                astraea::graphics::GuestGpuImageDescriptor{
                    .base_address = target.base_address,
                    .format =
                        astraea::graphics::GuestGpuImageFormat::
                            r8g8b8a8_unorm,
                    .width = target.width,
                    .height = target.height,
                    .bytes_per_pixel =
                        kV3FirstRasterBytesPerPixel,
                    .logical_byte_count =
                        kV3FirstRasterLogicalByteCount,
                    .layout =
                        astraea::graphics::GuestGpuSurfaceLayout{
                            .kind =
                                astraea::graphics::
                                    GuestGpuSurfaceLayoutKind::
                                        gfx10_aligned_linear,
                            .pitch_pixels =
                                layout->pitch_pixels,
                            .pitch_bytes =
                                static_cast<std::uint32_t>(
                                    layout->pitch_bytes),
                            .base_alignment_bytes =
                                layout->base_alignment_bytes,
                            .surface_byte_count =
                                layout->surface_byte_count,
                        },
                },
        });
}

V3FirstRasterImageResult
register_v3_first_raster_image(
    const V3FirstRasterTargetPlan& plan,
    const astraea::graphics::GuestGpuAllocationAddressSpace& allocations,
    astraea::graphics::GuestGpuImageRegistry& images) {
    auto registered =
        images.register_view(
            plan.image,
            allocations);
    if (!registered.has_value()) {
        return V3FirstRasterImageResult::failure(
            V3FirstRasterImageError{
                .code =
                    V3FirstRasterImageErrorCode::
                        image_registration_failure,
                .registration_error =
                    registered.error(),
            });
    }

    return V3FirstRasterImageResult::success(
        registered.value());
}

}  // namespace astraea::research
