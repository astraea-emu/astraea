#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <astraea/core/result.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/memory_plan.hpp>
#include <astraea/execution/windows_memory.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

enum class GuestMemoryErrorCode {
    prepared_memory_unavailable,
    guest_memory_unmapped,
    guest_memory_permission_denied,
    guest_memory_range_overflow,
    host_size_unrepresentable,
    unterminated_string,
    host_allocation_failure,
};

struct GuestMemoryError {
    GuestMemoryErrorCode code =
        GuestMemoryErrorCode::guest_memory_unmapped;
    bool has_guest_address = false;
    std::uint64_t guest_address = 0;

    auto operator<=>(const GuestMemoryError&) const = default;
};

class GuestMemoryAccess {
public:
    GuestMemoryAccess(
        const astraea::loader::GuestImage& image,
        const LinuxPreparedMemory& prepared_memory) noexcept
        : image_(&image),
          prepared_plan_(&prepared_memory.plan()),
          prepared_epoch_(&prepared_memory.mapping_epoch_ref()),
          prepared_memory_available_(!prepared_memory.empty()) {}

    GuestMemoryAccess(
        const astraea::loader::GuestImage& image,
        const WindowsPreparedMemory& prepared_memory) noexcept
        : image_(&image),
          prepared_plan_(&prepared_memory.plan()),
          prepared_epoch_(&prepared_memory.mapping_epoch_ref()),
          prepared_memory_available_(!prepared_memory.empty()) {}

    using CopyResult =
        astraea::core::Result<std::size_t, GuestMemoryError>;
    using StringResult =
        astraea::core::Result<std::string, GuestMemoryError>;

    [[nodiscard]] CopyResult read(
        astraea::memory::GuestAddress address,
        std::span<std::byte> output) const noexcept;

    [[nodiscard]] CopyResult write(
        astraea::memory::GuestAddress address,
        std::span<const std::byte> input) const noexcept;

    // Validates the exact writable guest range without mutating memory.
    // Intended for multi-patch operations that must establish every expected
    // write before applying the first mutation.
    [[nodiscard]] CopyResult preflight_write(
        astraea::memory::GuestAddress address,
        std::size_t byte_count) const noexcept;

    [[nodiscard]] StringResult read_c_string(
        astraea::memory::GuestAddress address,
        std::size_t max_bytes) const;

    [[nodiscard]] bool is_exact_executable_address(
        astraea::memory::GuestAddress address) const noexcept;

    // Process-local identity of the *current* prepared native mapping
    // owner. It follows move semantics and changes on same-VA replacement.
    // Zero means unavailable. Does not pin memory against concurrent unmap.
    [[nodiscard]] std::uint64_t current_mapping_epoch() const noexcept {
        return image_ != nullptr && prepared_plan_ != nullptr &&
                       prepared_epoch_ != nullptr &&
                       prepared_memory_available_ &&
                       !prepared_plan_->regions.empty()
            ? *prepared_epoch_
            : 0U;
    }

private:
    enum class AccessKind {
        read,
        write,
    };

    [[nodiscard]] astraea::core::Result<
        std::size_t,
        GuestMemoryError>
    validate(
        astraea::memory::GuestAddress address,
        std::size_t byte_count,
        AccessKind access) const noexcept;

    const astraea::loader::GuestImage* image_ = nullptr;
    const ExecutionMemoryPlan* prepared_plan_ = nullptr;
    const std::uint64_t* prepared_epoch_ = nullptr;
    bool prepared_memory_available_ = false;
};

}  // namespace astraea::execution
