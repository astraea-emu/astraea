#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_address.hpp>

namespace astraea::graphics {

// Stable guest-domain storage identity.
//
// An allocation is not a buffer, image, CPU pointer, host allocation, Vulkan
// object, or VkDeviceAddress. Typed resource views are layered above it.
struct GuestGpuAllocationId {
    std::uint64_t value = 0;

    auto operator<=>(const GuestGpuAllocationId&) const = default;
};

struct GuestGpuAllocation {
    GuestGpuAllocationId id;
    GpuVirtualAddress base;
    std::uint64_t byte_size = 0;

    auto operator<=>(const GuestGpuAllocation&) const = default;
};

enum class GuestGpuAllocationRegistrationErrorCode {
    zero_sized_allocation,
    address_range_overflow,
    overlapping_allocation,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct GuestGpuAllocationRegistrationError {
    GuestGpuAllocationRegistrationErrorCode code =
        GuestGpuAllocationRegistrationErrorCode::zero_sized_allocation;
    GpuVirtualAddress base;
    std::uint64_t byte_size = 0;
    std::optional<GuestGpuAllocationId> conflicting_allocation_id;

    auto operator<=>(const GuestGpuAllocationRegistrationError&) const =
        default;
};

using GuestGpuAllocationRegistrationResult =
    astraea::core::Result<
        GuestGpuAllocationId,
        GuestGpuAllocationRegistrationError>;

enum class GuestGpuAllocationResolutionErrorCode {
    zero_sized_request,
    address_range_overflow,
    unmapped_range,
    partially_mapped_range,
};

struct GuestGpuAllocationResolutionError {
    GuestGpuAllocationResolutionErrorCode code =
        GuestGpuAllocationResolutionErrorCode::unmapped_range;
    GpuVirtualAddress address;
    std::uint64_t byte_count = 0;
    std::optional<GuestGpuAllocationId> intersecting_allocation_id;

    auto operator<=>(const GuestGpuAllocationResolutionError&) const =
        default;
};

struct GuestGpuAllocationResolution {
    GuestGpuAllocationId allocation_id;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_count = 0;

    auto operator<=>(const GuestGpuAllocationResolution&) const = default;
};

using GuestGpuAllocationResolutionResult =
    astraea::core::Result<
        GuestGpuAllocationResolution,
        GuestGpuAllocationResolutionError>;

class GuestGpuAllocationAddressSpace {
public:
    // Registers one non-empty half-open GPU-domain allocation. Overlap is
    // rejected atomically; adjacency is allowed. Stable IDs are never reused.
    [[nodiscard]] GuestGpuAllocationRegistrationResult
    register_allocation(
        GpuVirtualAddress base,
        std::uint64_t byte_size);

    // Resolves one non-empty half-open request wholly contained in exactly one
    // registered allocation. Requests are never stitched across adjacency.
    [[nodiscard]] GuestGpuAllocationResolutionResult
    resolve_range(
        GpuVirtualAddress address,
        std::uint64_t byte_count) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return allocations_.size();
    }

    [[nodiscard]] const GuestGpuAllocation* entry_at(
        GuestGpuAllocationId id) const noexcept;

private:
    std::vector<GuestGpuAllocation> allocations_;
};

}  // namespace astraea::graphics
