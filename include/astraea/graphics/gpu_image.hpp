#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_address.hpp>
#include <astraea/graphics/gpu_allocation_address_space.hpp>

namespace astraea::graphics {

enum class GuestGpuImageFormat {
    r8g8b8a8_unorm,
};

enum class GuestGpuSurfaceLayoutKind {
    gfx10_aligned_linear,
};

struct GuestGpuSurfaceLayout {
    GuestGpuSurfaceLayoutKind kind =
        GuestGpuSurfaceLayoutKind::gfx10_aligned_linear;
    std::uint32_t pitch_pixels = 0;
    std::uint32_t pitch_bytes = 0;
    std::uint32_t base_alignment_bytes = 0;
    std::uint64_t surface_byte_count = 0;

    auto operator<=>(const GuestGpuSurfaceLayout&) const = default;
};

struct GuestGpuImageDescriptor {
    GpuVirtualAddress base_address;
    GuestGpuImageFormat format =
        GuestGpuImageFormat::r8g8b8a8_unorm;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bytes_per_pixel = 0;
    std::uint64_t logical_byte_count = 0;
    GuestGpuSurfaceLayout layout;

    auto operator<=>(const GuestGpuImageDescriptor&) const = default;
};

struct GuestGpuImageId {
    std::uint64_t value = 0;

    auto operator<=>(const GuestGpuImageId&) const = default;
};

struct GuestGpuImageView {
    GuestGpuImageId id;
    GuestGpuAllocationId allocation_id;
    std::uint64_t allocation_byte_offset = 0;
    GuestGpuImageDescriptor descriptor;

    auto operator<=>(const GuestGpuImageView&) const = default;
};

enum class GuestGpuImageRegistrationErrorCode {
    zero_dimension,
    zero_bytes_per_pixel,
    logical_size_overflow,
    logical_size_mismatch,
    zero_pitch,
    pitch_size_overflow,
    pitch_size_mismatch,
    pitch_smaller_than_width,
    zero_base_alignment,
    misaligned_base_address,
    zero_surface_size,
    surface_size_overflow,
    surface_size_mismatch,
    allocation_resolution_failure,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct GuestGpuImageRegistrationError {
    GuestGpuImageRegistrationErrorCode code =
        GuestGpuImageRegistrationErrorCode::zero_dimension;
    std::optional<GuestGpuAllocationResolutionError>
        allocation_resolution_error;

    auto operator<=>(const GuestGpuImageRegistrationError&) const =
        default;
};

using GuestGpuImageRegistrationResult =
    astraea::core::Result<
        GuestGpuImageId,
        GuestGpuImageRegistrationError>;

enum class GuestGpuImageResolutionErrorCode {
    not_found,
    ambiguous,
};

struct GuestGpuImageResolutionError {
    GuestGpuImageResolutionErrorCode code =
        GuestGpuImageResolutionErrorCode::not_found;
    std::size_t match_count = 0;
    std::optional<GuestGpuImageId> first_matching_image_id;

    auto operator<=>(const GuestGpuImageResolutionError&) const =
        default;
};

using GuestGpuImageResolutionResult =
    astraea::core::Result<
        GuestGpuImageView,
        GuestGpuImageResolutionError>;

class GuestGpuImageRegistry {
public:
    // Registers one typed image interpretation over a guest allocation.
    // Image views may alias/overlap each other; allocation storage itself must
    // already be uniquely registered in GuestGpuAllocationAddressSpace.
    [[nodiscard]] GuestGpuImageRegistrationResult
    register_view(
        const GuestGpuImageDescriptor& descriptor,
        const GuestGpuAllocationAddressSpace& allocations);

    // Finds an exact typed image interpretation. Multiple identical views are
    // preserved as distinct identities and therefore resolve as ambiguous.
    [[nodiscard]] GuestGpuImageResolutionResult
    resolve_exact(
        const GuestGpuImageDescriptor& descriptor) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return views_.size();
    }

    [[nodiscard]] const GuestGpuImageView* entry_at(
        GuestGpuImageId id) const noexcept;

private:
    std::vector<GuestGpuImageView> views_;
};

}  // namespace astraea::graphics
