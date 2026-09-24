#include <astraea/graphics/gpu_allocation_address_space.hpp>

#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace astraea::graphics {
namespace {

[[nodiscard]] bool checked_end(
    GpuVirtualAddress base,
    std::uint64_t byte_size,
    std::uint64_t& end) noexcept {
    if (base.value >
        std::numeric_limits<std::uint64_t>::max() - byte_size) {
        return false;
    }

    end = base.value + byte_size;
    return true;
}

[[nodiscard]] bool ranges_overlap(
    std::uint64_t left_begin,
    std::uint64_t left_end,
    std::uint64_t right_begin,
    std::uint64_t right_end) noexcept {
    return left_begin < right_end &&
           right_begin < left_end;
}

[[nodiscard]] GuestGpuAllocationRegistrationError
registration_error(
    GuestGpuAllocationRegistrationErrorCode code,
    GpuVirtualAddress base,
    std::uint64_t byte_size,
    std::optional<GuestGpuAllocationId> conflicting =
        std::nullopt) noexcept {
    return GuestGpuAllocationRegistrationError{
        .code = code,
        .base = base,
        .byte_size = byte_size,
        .conflicting_allocation_id = conflicting,
    };
}

[[nodiscard]] GuestGpuAllocationResolutionError
resolution_error(
    GuestGpuAllocationResolutionErrorCode code,
    GpuVirtualAddress address,
    std::uint64_t byte_count,
    std::optional<GuestGpuAllocationId> intersecting =
        std::nullopt) noexcept {
    return GuestGpuAllocationResolutionError{
        .code = code,
        .address = address,
        .byte_count = byte_count,
        .intersecting_allocation_id = intersecting,
    };
}

}  // namespace

GuestGpuAllocationRegistrationResult
GuestGpuAllocationAddressSpace::register_allocation(
    GpuVirtualAddress base,
    std::uint64_t byte_size) {
    if (byte_size == 0U) {
        return GuestGpuAllocationRegistrationResult::failure(
            registration_error(
                GuestGpuAllocationRegistrationErrorCode::
                    zero_sized_allocation,
                base,
                byte_size));
    }

    std::uint64_t end = 0;
    if (!checked_end(base, byte_size, end)) {
        return GuestGpuAllocationRegistrationResult::failure(
            registration_error(
                GuestGpuAllocationRegistrationErrorCode::
                    address_range_overflow,
                base,
                byte_size));
    }

    for (const auto& allocation : allocations_) {
        std::uint64_t allocation_end = 0;
        if (!checked_end(
                allocation.base,
                allocation.byte_size,
                allocation_end)) {
            return GuestGpuAllocationRegistrationResult::failure(
                registration_error(
                    GuestGpuAllocationRegistrationErrorCode::
                        address_range_overflow,
                    allocation.base,
                    allocation.byte_size,
                    allocation.id));
        }

        if (ranges_overlap(
                base.value,
                end,
                allocation.base.value,
                allocation_end)) {
            return GuestGpuAllocationRegistrationResult::failure(
                registration_error(
                    GuestGpuAllocationRegistrationErrorCode::
                        overlapping_allocation,
                    base,
                    byte_size,
                    allocation.id));
        }
    }

    const auto id =
        GuestGpuAllocationId{
            .value =
                static_cast<std::uint64_t>(
                    allocations_.size()),
        };

    try {
        allocations_.push_back(
            GuestGpuAllocation{
                .id = id,
                .base = base,
                .byte_size = byte_size,
            });
    } catch (const std::bad_alloc&) {
        return GuestGpuAllocationRegistrationResult::failure(
            registration_error(
                GuestGpuAllocationRegistrationErrorCode::
                    host_allocation_failure,
                base,
                byte_size));
    } catch (const std::length_error&) {
        return GuestGpuAllocationRegistrationResult::failure(
            registration_error(
                GuestGpuAllocationRegistrationErrorCode::
                    host_size_unrepresentable,
                base,
                byte_size));
    }

    return GuestGpuAllocationRegistrationResult::success(id);
}

GuestGpuAllocationResolutionResult
GuestGpuAllocationAddressSpace::resolve_range(
    GpuVirtualAddress address,
    std::uint64_t byte_count) const noexcept {
    if (byte_count == 0U) {
        return GuestGpuAllocationResolutionResult::failure(
            resolution_error(
                GuestGpuAllocationResolutionErrorCode::
                    zero_sized_request,
                address,
                byte_count));
    }

    std::uint64_t request_end = 0;
    if (!checked_end(address, byte_count, request_end)) {
        return GuestGpuAllocationResolutionResult::failure(
            resolution_error(
                GuestGpuAllocationResolutionErrorCode::
                    address_range_overflow,
                address,
                byte_count));
    }

    std::optional<GuestGpuAllocationId>
        intersecting_allocation_id;

    for (const auto& allocation : allocations_) {
        std::uint64_t allocation_end = 0;
        if (!checked_end(
                allocation.base,
                allocation.byte_size,
                allocation_end)) {
            continue;
        }

        if (address.value >= allocation.base.value &&
            request_end <= allocation_end) {
            return GuestGpuAllocationResolutionResult::success(
                GuestGpuAllocationResolution{
                    .allocation_id = allocation.id,
                    .byte_offset =
                        address.value -
                        allocation.base.value,
                    .byte_count = byte_count,
                });
        }

        if (ranges_overlap(
                address.value,
                request_end,
                allocation.base.value,
                allocation_end)) {
            intersecting_allocation_id =
                allocation.id;
        }
    }

    if (intersecting_allocation_id.has_value()) {
        return GuestGpuAllocationResolutionResult::failure(
            resolution_error(
                GuestGpuAllocationResolutionErrorCode::
                    partially_mapped_range,
                address,
                byte_count,
                intersecting_allocation_id));
    }

    return GuestGpuAllocationResolutionResult::failure(
        resolution_error(
            GuestGpuAllocationResolutionErrorCode::
                unmapped_range,
            address,
            byte_count));
}

const GuestGpuAllocation*
GuestGpuAllocationAddressSpace::entry_at(
    GuestGpuAllocationId id) const noexcept {
    for (const auto& allocation : allocations_) {
        if (allocation.id == id) {
            return &allocation;
        }
    }

    return nullptr;
}

}  // namespace astraea::graphics
