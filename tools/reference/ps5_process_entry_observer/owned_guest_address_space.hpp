#pragma once

// C1 research-only checked *virtual* guest memory. This is not an
// isolation mechanism for arbitrary native guest instructions. The caller
// owns all backing storage, which must outlive the address-space object.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <astraea/core/result.hpp>
#include <astraea/memory/mapping.hpp>

namespace astraea::research {

enum class OwnedGuestMemoryError {
    zero_length,
    address_overflow,
    region_overlap,
    backing_alias,
    region_limit,
    unmapped,
    permission_denied,
};

struct OwnedGuestMemoryFault {
    OwnedGuestMemoryError code;
    std::uint64_t guest_address = 0U;
};

class OwnedGuestAddressSpace {
public:
    static constexpr std::size_t kMaxRegions = 8U;
    using Result = astraea::core::Result<std::size_t, OwnedGuestMemoryFault>;

    // A mapping's virtual permissions are distinct from host page
    // permissions. W+X virtual permissions are representable for fidelity
    // but this class NEVER makes backing memory executable.
    // Host backing spans must be disjoint, and guest ranges may not overlap.
    [[nodiscard]] Result map(
        std::uint64_t guest_base,
        std::span<std::byte> owned_backing,
        astraea::memory::GuestPermissions virtual_permissions) noexcept;

    // Each access is within ONE registered region. Cross-region access,
    // address overflow, unmapped bytes and missing virtual permission all
    // refuse before copying any bytes; no partial writes on failure.
    [[nodiscard]] Result read(
        std::uint64_t guest_address, std::span<std::byte> output) const noexcept;
    // Validate an entire would-be relocation write without mutating guest
    // bytes. No host memory is made executable or writable by this API.
    [[nodiscard]] Result preflight_write(
        std::uint64_t guest_address, std::size_t byte_count) const noexcept;
    [[nodiscard]] Result write(
        std::uint64_t guest_address,
        std::span<const std::byte> source) noexcept;
    [[nodiscard]] Result fetch(
        std::uint64_t guest_address, std::span<std::byte> output) const noexcept;

    [[nodiscard]] std::size_t region_count() const noexcept {
        return count_;
    }

private:
    struct Region {
        std::uint64_t base = 0U;
        std::span<std::byte> backing;
        astraea::memory::GuestPermissions perms{};
    };
    std::array<Region, kMaxRegions> regions_{};
    std::size_t count_ = 0U;

    [[nodiscard]] Result copy_out(
        std::uint64_t address, std::span<std::byte> output,
        astraea::memory::GuestPermission required) const noexcept;
};

} // namespace astraea::research
