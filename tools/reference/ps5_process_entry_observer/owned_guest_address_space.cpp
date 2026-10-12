#include "owned_guest_address_space.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace astraea::research {
namespace {
using Error = OwnedGuestMemoryError;
using Result = OwnedGuestAddressSpace::Result;

[[nodiscard]] Result fail(Error code, std::uint64_t addr) noexcept {
    return Result::failure(OwnedGuestMemoryFault{code, addr});
}

[[nodiscard]] bool within(
    std::uint64_t base, std::size_t length,
    std::uint64_t address, std::size_t requested) noexcept {
    // Bounds verified by caller's overflow checks.
    return address >= base &&
        address - base <= length &&
        requested <= length - static_cast<std::size_t>(address - base);
}
} // namespace

OwnedGuestAddressSpace::Result OwnedGuestAddressSpace::map(
    std::uint64_t guest_base, std::span<std::byte> owned_backing,
    astraea::memory::GuestPermissions virtual_permissions) noexcept {
    if (owned_backing.empty()) return fail(Error::zero_length, guest_base);
    if (owned_backing.size() >
        std::numeric_limits<std::uint64_t>::max() - guest_base)
        return fail(Error::address_overflow, guest_base);
    if (count_ >= regions_.size())
        return fail(Error::region_limit, guest_base);
    const auto end = guest_base + owned_backing.size();

    for (std::size_t i = 0U; i < count_; ++i) {
        const auto& other = regions_[i];
        const auto other_end = other.base + other.backing.size();
        if (guest_base < other_end && other.base < end)
            return fail(Error::region_overlap, guest_base);

        // The region table must not introduce a second virtual alias of
        // writable native backing. A caller may still hold external host
        // aliases, so this is an invariant, NOT hostile-code containment.
        const auto a = reinterpret_cast<std::uintptr_t>(
            owned_backing.data());
        const auto b = reinterpret_cast<std::uintptr_t>(
            other.backing.data());
        if (owned_backing.size() >
                std::numeric_limits<std::uintptr_t>::max() - a ||
            other.backing.size() >
                std::numeric_limits<std::uintptr_t>::max() - b)
            return fail(Error::backing_alias, guest_base);
        if (a < b + other.backing.size() &&
            b < a + owned_backing.size())
            return fail(Error::backing_alias, guest_base);
    }
    regions_[count_] = Region{
        .base = guest_base,
        .backing = owned_backing,
        .perms = virtual_permissions,
    };
    return Result::success(count_++);
}

OwnedGuestAddressSpace::Result OwnedGuestAddressSpace::copy_out(
    std::uint64_t address, std::span<std::byte> output,
    astraea::memory::GuestPermission required) const noexcept {
    if (output.empty()) return fail(Error::zero_length, address);
    if (output.size() >
        std::numeric_limits<std::uint64_t>::max() - address)
        return fail(Error::address_overflow, address);
    for (std::size_t i = 0U; i < count_; ++i) {
        const auto& region = regions_[i];
        if (within(region.base, region.backing.size(),
                   address, output.size())) {
            if (!region.perms.has(required))
                return fail(Error::permission_denied, address);
            const auto offset = static_cast<std::size_t>(
                address - region.base);
            std::memmove(output.data(), region.backing.data() + offset,
                         output.size());
            return Result::success(output.size());
        }
    }
    return fail(Error::unmapped, address);
}

OwnedGuestAddressSpace::Result OwnedGuestAddressSpace::read(
    std::uint64_t address, std::span<std::byte> output) const noexcept {
    return copy_out(
        address, output, astraea::memory::GuestPermission::read);
}

OwnedGuestAddressSpace::Result OwnedGuestAddressSpace::fetch(
    std::uint64_t address, std::span<std::byte> output) const noexcept {
    return copy_out(
        address, output, astraea::memory::GuestPermission::execute);
}

OwnedGuestAddressSpace::Result OwnedGuestAddressSpace::preflight_write(
    std::uint64_t address, std::size_t byte_count) const noexcept {
    if (byte_count == 0U) return fail(Error::zero_length, address);
    if (byte_count > std::numeric_limits<std::uint64_t>::max() - address)
        return fail(Error::address_overflow, address);
    for (std::size_t i = 0U; i < count_; ++i) {
        const auto& region = regions_[i];
        if (within(region.base, region.backing.size(),
                   address, byte_count)) {
            if (!region.perms.has(
                    astraea::memory::GuestPermission::write))
                return fail(Error::permission_denied, address);
            return Result::success(byte_count);
        }
    }
    return fail(Error::unmapped, address);
}

OwnedGuestAddressSpace::Result OwnedGuestAddressSpace::write(
    std::uint64_t address, std::span<const std::byte> source) noexcept {
    const auto checked = preflight_write(address, source.size());
    if (!checked.has_value()) return checked;
    for (std::size_t i = 0U; i < count_; ++i) {
        auto& region = regions_[i];
        if (within(region.base, region.backing.size(),
                   address, source.size())) {
            const auto offset = static_cast<std::size_t>(
                address - region.base);
            std::memmove(region.backing.data() + offset, source.data(),
                         source.size());
            return Result::success(source.size());
        }
    }
    return fail(Error::unmapped, address);
}

} // namespace astraea::research
