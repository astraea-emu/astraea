#pragma once

#include <compare>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_allocation_address_space.hpp>
#include <astraea/graphics/gpu_image.hpp>
#include <astraea/research/v3_first_raster_target.hpp>

namespace astraea::research {

enum class V3FirstRasterImageErrorCode {
    raster_target_plan_mismatch,
    image_registration_failure,
};

struct V3FirstRasterImageError {
    V3FirstRasterImageErrorCode code =
        V3FirstRasterImageErrorCode::raster_target_plan_mismatch;
    std::optional<astraea::graphics::GuestGpuImageRegistrationError>
        image_registration_error;

    auto operator<=>(const V3FirstRasterImageError&) const = default;
};

using V3FirstRasterImageResult =
    astraea::core::Result<
        astraea::graphics::GuestGpuImageId,
        V3FirstRasterImageError>;

// Registers only the exact V3 first-raster target plan as a typed guest image
// view over storage that already exists in the generic allocation registry.
//
// This adapter performs no allocation, Vulkan materialization, tiling
// conversion, draw execution, or LinkShaders behavior.
[[nodiscard]] V3FirstRasterImageResult
register_v3_first_raster_image(
    const V3FirstRasterTargetPlan& plan,
    const astraea::graphics::GuestGpuAllocationAddressSpace& allocations,
    astraea::graphics::GuestGpuImageRegistry& images);

}  // namespace astraea::research
