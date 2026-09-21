#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <astraea/core/result.hpp>
#include <astraea/execution/linux_memory.hpp>
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
          prepared_memory_(&prepared_memory) {}

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

    [[nodiscard]] StringResult read_c_string(
        astraea::memory::GuestAddress address,
        std::size_t max_bytes) const;

    [[nodiscard]] bool is_exact_executable_address(
        astraea::memory::GuestAddress address) const noexcept;

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
    const LinuxPreparedMemory* prepared_memory_ = nullptr;
};

}  // namespace astraea::execution
