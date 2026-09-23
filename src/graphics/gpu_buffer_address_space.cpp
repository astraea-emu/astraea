#include <astraea/graphics/gpu_buffer_address_space.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace astraea::graphics {
namespace {

[[nodiscard]] bool checked_end(
    GpuVirtualAddress base,
    std::uint64_t byte_size,
    std::uint64_t& end) noexcept {
    if (base.value >
        std::numeric_limits<std::uint64_t>::max() -
            byte_size) {
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

[[nodiscard]] GuestGpuBufferRegistrationError
registration_error(
    GuestGpuBufferRegistrationErrorCode code,
    GpuVirtualAddress base,
    std::uint64_t byte_size,
    std::optional<GuestGpuBufferId> conflicting =
        std::nullopt) noexcept {
    return GuestGpuBufferRegistrationError{
        .code = code,
        .base = base,
        .byte_size = byte_size,
        .conflicting_buffer_id = conflicting,
    };
}

[[nodiscard]] GuestGpuBufferResolutionError
resolution_error(
    GuestGpuBufferResolutionErrorCode code,
    GpuVirtualAddress address,
    std::uint64_t byte_count,
    std::optional<GuestGpuBufferId> intersecting =
        std::nullopt) noexcept {
    return GuestGpuBufferResolutionError{
        .code = code,
        .address = address,
        .byte_count = byte_count,
        .intersecting_buffer_id = intersecting,
    };
}

}  // namespace

GuestGpuBufferRegistrationResult
GuestGpuBufferAddressSpace::register_buffer(
    GpuVirtualAddress base,
    std::uint64_t byte_size) {
    if (byte_size == 0U) {
        return GuestGpuBufferRegistrationResult::failure(
            registration_error(
                GuestGpuBufferRegistrationErrorCode::
                    zero_sized_region,
                base,
                byte_size));
    }

    std::uint64_t end = 0;
    if (!checked_end(base, byte_size, end)) {
        return GuestGpuBufferRegistrationResult::failure(
            registration_error(
                GuestGpuBufferRegistrationErrorCode::
                    address_range_overflow,
                base,
                byte_size));
    }

    for (const auto& buffer : buffers_) {
        std::uint64_t buffer_end = 0;
        if (!checked_end(
                buffer.base,
                buffer.byte_size,
                buffer_end)) {
            return GuestGpuBufferRegistrationResult::failure(
                registration_error(
                    GuestGpuBufferRegistrationErrorCode::
                        address_range_overflow,
                    buffer.base,
                    buffer.byte_size,
                    buffer.id));
        }

        if (ranges_overlap(
                base.value,
                end,
                buffer.base.value,
                buffer_end)) {
            return GuestGpuBufferRegistrationResult::failure(
                registration_error(
                    GuestGpuBufferRegistrationErrorCode::
                        overlapping_region,
                    base,
                    byte_size,
                    buffer.id));
        }
    }

    if constexpr (
        std::numeric_limits<std::size_t>::digits >
        std::numeric_limits<std::uint64_t>::digits) {
        if (buffers_.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max())) {
            return GuestGpuBufferRegistrationResult::failure(
                registration_error(
                    GuestGpuBufferRegistrationErrorCode::
                        host_size_unrepresentable,
                    base,
                    byte_size));
        }
    }

    const auto id =
        GuestGpuBufferId{
            .value =
                static_cast<std::uint64_t>(
                    buffers_.size()),
        };

    try {
        buffers_.push_back(
            GuestGpuBufferRegion{
                .id = id,
                .base = base,
                .byte_size = byte_size,
            });
    } catch (const std::bad_alloc&) {
        return GuestGpuBufferRegistrationResult::failure(
            registration_error(
                GuestGpuBufferRegistrationErrorCode::
                    host_allocation_failure,
                base,
                byte_size));
    } catch (const std::length_error&) {
        return GuestGpuBufferRegistrationResult::failure(
            registration_error(
                GuestGpuBufferRegistrationErrorCode::
                    host_size_unrepresentable,
                base,
                byte_size));
    }

    return GuestGpuBufferRegistrationResult::success(id);
}

GuestGpuBufferResolutionResult
GuestGpuBufferAddressSpace::resolve_range(
    GpuVirtualAddress address,
    std::uint64_t byte_count) const noexcept {
    if (byte_count == 0U) {
        return GuestGpuBufferResolutionResult::failure(
            resolution_error(
                GuestGpuBufferResolutionErrorCode::
                    zero_sized_request,
                address,
                byte_count));
    }

    std::uint64_t request_end = 0;
    if (!checked_end(
            address,
            byte_count,
            request_end)) {
        return GuestGpuBufferResolutionResult::failure(
            resolution_error(
                GuestGpuBufferResolutionErrorCode::
                    address_range_overflow,
                address,
                byte_count));
    }

    std::optional<GuestGpuBufferId>
        intersecting_buffer_id;

    for (const auto& buffer : buffers_) {
        std::uint64_t buffer_end = 0;
        if (!checked_end(
                buffer.base,
                buffer.byte_size,
                buffer_end)) {
            continue;
        }

        if (address.value >= buffer.base.value &&
            request_end <= buffer_end) {
            return GuestGpuBufferResolutionResult::success(
                GuestGpuBufferResolution{
                    .buffer_id = buffer.id,
                    .byte_offset =
                        address.value -
                        buffer.base.value,
                    .byte_count = byte_count,
                });
        }

        if (ranges_overlap(
                address.value,
                request_end,
                buffer.base.value,
                buffer_end)) {
            intersecting_buffer_id = buffer.id;
        }
    }

    if (intersecting_buffer_id.has_value()) {
        return GuestGpuBufferResolutionResult::failure(
            resolution_error(
                GuestGpuBufferResolutionErrorCode::
                    partially_mapped_range,
                address,
                byte_count,
                intersecting_buffer_id));
    }

    return GuestGpuBufferResolutionResult::failure(
        resolution_error(
            GuestGpuBufferResolutionErrorCode::
                unmapped_range,
            address,
            byte_count));
}

const GuestGpuBufferRegion*
GuestGpuBufferAddressSpace::entry_at(
    GuestGpuBufferId id) const noexcept {
    if constexpr (
        std::numeric_limits<std::uint64_t>::digits >
        std::numeric_limits<std::size_t>::digits) {
        if (id.value >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return nullptr;
        }
    }

    const auto index =
        static_cast<std::size_t>(id.value);
    if (index >= buffers_.size()) {
        return nullptr;
    }
    return &buffers_[index];
}

GpuMemoryWriteResolutionResult
resolve_gpu_memory_write(
    const GraphicsIrGpuMemoryWrite& operation,
    const GuestGpuBufferAddressSpace& address_space) noexcept {
    constexpr std::uint64_t kDwordBytes = 4U;
    const auto value_count =
        operation.values.size();

    if constexpr (
        std::numeric_limits<std::size_t>::digits >
        std::numeric_limits<std::uint64_t>::digits) {
        if (value_count >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max())) {
            return GpuMemoryWriteResolutionResult::failure(
                GpuMemoryWriteResolutionError{
                    .code =
                        GpuMemoryWriteResolutionErrorCode::
                            payload_size_unrepresentable,
                    .address_resolution_error =
                        std::nullopt,
                });
        }
    }

    const auto value_count_u64 =
        static_cast<std::uint64_t>(value_count);
    if (value_count_u64 >
        std::numeric_limits<std::uint64_t>::max() /
            kDwordBytes) {
        return GpuMemoryWriteResolutionResult::failure(
            GpuMemoryWriteResolutionError{
                .code =
                    GpuMemoryWriteResolutionErrorCode::
                        payload_size_unrepresentable,
                .address_resolution_error =
                    std::nullopt,
            });
    }

    const auto byte_count =
        value_count_u64 * kDwordBytes;

    auto resolved =
        address_space.resolve_range(
            operation.destination,
            byte_count);
    if (!resolved.has_value()) {
        return GpuMemoryWriteResolutionResult::failure(
            GpuMemoryWriteResolutionError{
                .code =
                    GpuMemoryWriteResolutionErrorCode::
                        address_resolution_failure,
                .address_resolution_error =
                    resolved.error(),
            });
    }

    return GpuMemoryWriteResolutionResult::success(
        resolved.value());
}

}  // namespace astraea::graphics
