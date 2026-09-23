#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/gpu_address.hpp>
#include <astraea/graphics/graphics_ir.hpp>

namespace astraea::graphics {

struct GuestGpuBufferId {
    std::uint64_t value = 0;

    auto operator<=>(const GuestGpuBufferId&) const = default;
};

struct GuestGpuBufferRegion {
    GuestGpuBufferId id;
    GpuVirtualAddress base;
    std::uint64_t byte_size = 0;

    auto operator<=>(const GuestGpuBufferRegion&) const = default;
};

enum class GuestGpuBufferRegistrationErrorCode {
    zero_sized_region,
    address_range_overflow,
    overlapping_region,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct GuestGpuBufferRegistrationError {
    GuestGpuBufferRegistrationErrorCode code =
        GuestGpuBufferRegistrationErrorCode::zero_sized_region;
    GpuVirtualAddress base;
    std::uint64_t byte_size = 0;
    std::optional<GuestGpuBufferId> conflicting_buffer_id;

    auto operator<=>(const GuestGpuBufferRegistrationError&) const = default;
};

using GuestGpuBufferRegistrationResult =
    astraea::core::Result<
        GuestGpuBufferId,
        GuestGpuBufferRegistrationError>;

enum class GuestGpuBufferResolutionErrorCode {
    zero_sized_request,
    address_range_overflow,
    unmapped_range,
    partially_mapped_range,
};

struct GuestGpuBufferResolutionError {
    GuestGpuBufferResolutionErrorCode code =
        GuestGpuBufferResolutionErrorCode::unmapped_range;
    GpuVirtualAddress address;
    std::uint64_t byte_count = 0;
    std::optional<GuestGpuBufferId> intersecting_buffer_id;

    auto operator<=>(const GuestGpuBufferResolutionError&) const = default;
};

struct GuestGpuBufferResolution {
    GuestGpuBufferId buffer_id;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_count = 0;

    auto operator<=>(const GuestGpuBufferResolution&) const = default;
};

using GuestGpuBufferResolutionResult =
    astraea::core::Result<
        GuestGpuBufferResolution,
        GuestGpuBufferResolutionError>;

class GuestGpuBufferAddressSpace {
public:
    // Registers one non-empty half-open GPU-domain range. Overlap is rejected
    // atomically; adjacency is allowed. Logical IDs are never reused in W1.
    [[nodiscard]] GuestGpuBufferRegistrationResult
    register_buffer(
        GpuVirtualAddress base,
        std::uint64_t byte_size);

    // Resolves one non-empty half-open request wholly contained in one
    // registered buffer. Requests are never stitched across adjacent buffers.
    [[nodiscard]] GuestGpuBufferResolutionResult
    resolve_range(
        GpuVirtualAddress address,
        std::uint64_t byte_count) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept {
        return buffers_.size();
    }

    [[nodiscard]] const GuestGpuBufferRegion* entry_at(
        GuestGpuBufferId id) const noexcept;

private:
    std::vector<GuestGpuBufferRegion> buffers_;
};

enum class GpuMemoryWriteResolutionErrorCode {
    payload_size_unrepresentable,
    address_resolution_failure,
};

struct GpuMemoryWriteResolutionError {
    GpuMemoryWriteResolutionErrorCode code =
        GpuMemoryWriteResolutionErrorCode::
            payload_size_unrepresentable;
    std::optional<GuestGpuBufferResolutionError>
        address_resolution_error;

    auto operator<=>(const GpuMemoryWriteResolutionError&) const = default;
};

using GpuMemoryWriteResolutionResult =
    astraea::core::Result<
        GuestGpuBufferResolution,
        GpuMemoryWriteResolutionError>;

// Resolves W0's typed memory-write range only. It does not copy payload bytes
// and does not mutate the address space or any backing storage.
[[nodiscard]] GpuMemoryWriteResolutionResult
resolve_gpu_memory_write(
    const GraphicsIrGpuMemoryWrite& operation,
    const GuestGpuBufferAddressSpace& address_space) noexcept;

}  // namespace astraea::graphics
