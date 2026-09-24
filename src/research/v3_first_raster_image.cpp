#include <astraea/research/v3_first_raster_image.hpp>

#include <cstdint>

#include <astraea/graphics/gfx10_surface_layout.hpp>

namespace astraea::research {
namespace {

[[nodiscard]] bool is_exact_first_target(
    const V3FirstRasterTargetPlan& plan) noexcept {
    const auto expected_layout =
        astraea::graphics::
            compute_gfx10_aligned_linear_2d_layout(
                kV3FirstRasterTargetWidth,
                kV3FirstRasterTargetHeight,
                kV3FirstRasterTargetBytesPerPixel);
    if (!expected_layout.has_value()) {
        return false;
    }

    const auto expected_logical_bytes =
        static_cast<std::uint64_t>(
            kV3FirstRasterTargetWidth) *
        static_cast<std::uint64_t>(
            kV3FirstRasterTargetHeight) *
        static_cast<std::uint64_t>(
            kV3FirstRasterTargetBytesPerPixel);

    return
        plan.base_address.value != 0U &&
        (plan.base_address.value %
         expected_layout->base_alignment_bytes) == 0U &&
        plan.width == kV3FirstRasterTargetWidth &&
        plan.height == kV3FirstRasterTargetHeight &&
        plan.bytes_per_pixel ==
            kV3FirstRasterTargetBytesPerPixel &&
        plan.logical_byte_count ==
            expected_logical_bytes &&
        plan.surface_pitch_pixels ==
            expected_layout->pitch_pixels &&
        plan.surface_pitch_bytes ==
            expected_layout->pitch_bytes &&
        plan.surface_base_alignment_bytes ==
            expected_layout->base_alignment_bytes &&
        plan.surface_byte_count ==
            expected_layout->surface_byte_count &&
        plan.host_format ==
            V3FirstRasterTargetHostFormat::
                r8g8b8a8_unorm;
}

}  // namespace

V3FirstRasterImageResult
register_v3_first_raster_image(
    const V3FirstRasterTargetPlan& plan,
    const astraea::graphics::GuestGpuAllocationAddressSpace& allocations,
    astraea::graphics::GuestGpuImageRegistry& images) {
    if (!is_exact_first_target(plan)) {
        return V3FirstRasterImageResult::failure(
            V3FirstRasterImageError{
                .code =
                    V3FirstRasterImageErrorCode::
                        raster_target_plan_mismatch,
                .image_registration_error =
                    std::nullopt,
            });
    }

    const astraea::graphics::GuestGpuImageDescriptor descriptor{
        .base_address = plan.base_address,
        .format =
            astraea::graphics::GuestGpuImageFormat::
                r8g8b8a8_unorm,
        .width = plan.width,
        .height = plan.height,
        .bytes_per_pixel = plan.bytes_per_pixel,
        .logical_byte_count =
            plan.logical_byte_count,
        .layout =
            astraea::graphics::GuestGpuSurfaceLayout{
                .kind =
                    astraea::graphics::
                        GuestGpuSurfaceLayoutKind::
                            gfx10_aligned_linear,
                .pitch_pixels =
                    plan.surface_pitch_pixels,
                .pitch_bytes =
                    static_cast<std::uint32_t>(
                        plan.surface_pitch_bytes),
                .base_alignment_bytes =
                    plan.surface_base_alignment_bytes,
                .surface_byte_count =
                    plan.surface_byte_count,
            },
    };

    auto registered =
        images.register_view(
            descriptor,
            allocations);
    if (!registered.has_value()) {
        return V3FirstRasterImageResult::failure(
            V3FirstRasterImageError{
                .code =
                    V3FirstRasterImageErrorCode::
                        image_registration_failure,
                .image_registration_error =
                    registered.error(),
            });
    }

    return V3FirstRasterImageResult::success(
        registered.value());
}

}  // namespace astraea::research
