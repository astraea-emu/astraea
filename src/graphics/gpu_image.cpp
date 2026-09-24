#include <astraea/graphics/gpu_image.hpp>

#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace astraea::graphics {
namespace {

[[nodiscard]] bool checked_multiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& product) noexcept {
    if (left != 0U &&
        right >
            std::numeric_limits<std::uint64_t>::max() /
                left) {
        return false;
    }

    product = left * right;
    return true;
}

[[nodiscard]] GuestGpuImageRegistrationError
registration_error(
    GuestGpuImageRegistrationErrorCode code,
    std::optional<GuestGpuAllocationResolutionError>
        allocation_error = std::nullopt) noexcept {
    return GuestGpuImageRegistrationError{
        .code = code,
        .allocation_resolution_error =
            allocation_error,
    };
}

}  // namespace

GuestGpuImageRegistrationResult
GuestGpuImageRegistry::register_view(
    const GuestGpuImageDescriptor& descriptor,
    const GuestGpuAllocationAddressSpace& allocations) {
    if (descriptor.width == 0U ||
        descriptor.height == 0U) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    zero_dimension));
    }

    if (descriptor.bytes_per_pixel == 0U) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    zero_bytes_per_pixel));
    }

    std::uint64_t pixel_count = 0;
    if (!checked_multiply(
            descriptor.width,
            descriptor.height,
            pixel_count)) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    logical_size_overflow));
    }

    std::uint64_t expected_logical_bytes = 0;
    if (!checked_multiply(
            pixel_count,
            descriptor.bytes_per_pixel,
            expected_logical_bytes)) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    logical_size_overflow));
    }

    if (descriptor.logical_byte_count !=
        expected_logical_bytes) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    logical_size_mismatch));
    }

    if (descriptor.layout.pitch_pixels == 0U ||
        descriptor.layout.pitch_bytes == 0U) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    zero_pitch));
    }

    if (descriptor.layout.pitch_pixels <
        descriptor.width) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    pitch_smaller_than_width));
    }

    std::uint64_t expected_pitch_bytes = 0;
    if (!checked_multiply(
            descriptor.layout.pitch_pixels,
            descriptor.bytes_per_pixel,
            expected_pitch_bytes)) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    pitch_size_overflow));
    }

    if (expected_pitch_bytes >
        std::numeric_limits<std::uint32_t>::max()) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    pitch_size_overflow));
    }

    if (descriptor.layout.pitch_bytes !=
        expected_pitch_bytes) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    pitch_size_mismatch));
    }

    if (descriptor.layout.base_alignment_bytes == 0U) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    zero_base_alignment));
    }

    if ((descriptor.base_address.value %
         descriptor.layout.base_alignment_bytes) != 0U) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    misaligned_base_address));
    }

    if (descriptor.layout.surface_byte_count == 0U) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    zero_surface_size));
    }

    std::uint64_t expected_surface_bytes = 0;
    if (!checked_multiply(
            descriptor.layout.pitch_bytes,
            descriptor.height,
            expected_surface_bytes)) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    surface_size_overflow));
    }

    if (descriptor.layout.surface_byte_count !=
        expected_surface_bytes) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    surface_size_mismatch));
    }

    auto allocation =
        allocations.resolve_range(
            descriptor.base_address,
            descriptor.layout.surface_byte_count);
    if (!allocation.has_value()) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    allocation_resolution_failure,
                allocation.error()));
    }

    const auto id =
        GuestGpuImageId{
            .value =
                static_cast<std::uint64_t>(
                    views_.size()),
        };

    try {
        views_.push_back(
            GuestGpuImageView{
                .id = id,
                .allocation_id =
                    allocation->allocation_id,
                .allocation_byte_offset =
                    allocation->byte_offset,
                .descriptor = descriptor,
            });
    } catch (const std::bad_alloc&) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GuestGpuImageRegistrationResult::failure(
            registration_error(
                GuestGpuImageRegistrationErrorCode::
                    host_size_unrepresentable));
    }

    return GuestGpuImageRegistrationResult::success(id);
}

GuestGpuImageResolutionResult
GuestGpuImageRegistry::resolve_exact(
    const GuestGpuImageDescriptor& descriptor) const noexcept {
    const GuestGpuImageView* first = nullptr;
    std::size_t match_count = 0;

    for (const auto& view : views_) {
        if (view.descriptor == descriptor) {
            ++match_count;
            if (first == nullptr) {
                first = &view;
            }
        }
    }

    if (match_count == 0U) {
        return GuestGpuImageResolutionResult::failure(
            GuestGpuImageResolutionError{
                .code =
                    GuestGpuImageResolutionErrorCode::
                        not_found,
                .match_count = 0U,
                .first_matching_image_id =
                    std::nullopt,
            });
    }

    if (match_count != 1U) {
        return GuestGpuImageResolutionResult::failure(
            GuestGpuImageResolutionError{
                .code =
                    GuestGpuImageResolutionErrorCode::
                        ambiguous,
                .match_count = match_count,
                .first_matching_image_id =
                    first->id,
            });
    }

    return GuestGpuImageResolutionResult::success(
        *first);
}

const GuestGpuImageView*
GuestGpuImageRegistry::entry_at(
    GuestGpuImageId id) const noexcept {
    for (const auto& view : views_) {
        if (view.id == id) {
            return &view;
        }
    }

    return nullptr;
}

}  // namespace astraea::graphics
