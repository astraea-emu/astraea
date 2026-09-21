#pragma once

#include <compare>
#include <cstdint>
#include <limits>

#include <astraea/core/result.hpp>

namespace astraea::memory {

enum class AddressErrorCode {
    guest_range_overflow,
    address_addition_overflow,
    size_addition_overflow,
    size_subtraction_underflow,
};

struct AddressError {
    AddressErrorCode code;
};

class GuestSize {
public:
    constexpr explicit GuestSize(std::uint64_t value = 0) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] static astraea::core::Result<GuestSize, AddressError> checked_add(
        GuestSize lhs,
        GuestSize rhs) {
        if (lhs.value_ > std::numeric_limits<std::uint64_t>::max() - rhs.value_) {
            return astraea::core::Result<GuestSize, AddressError>::failure(
                AddressError{AddressErrorCode::size_addition_overflow});
        }
        return astraea::core::Result<GuestSize, AddressError>::success(
            GuestSize{lhs.value_ + rhs.value_});
    }

    [[nodiscard]] static astraea::core::Result<GuestSize, AddressError> checked_subtract(
        GuestSize lhs,
        GuestSize rhs) {
        if (rhs.value_ > lhs.value_) {
            return astraea::core::Result<GuestSize, AddressError>::failure(
                AddressError{AddressErrorCode::size_subtraction_underflow});
        }
        return astraea::core::Result<GuestSize, AddressError>::success(
            GuestSize{lhs.value_ - rhs.value_});
    }

    auto operator<=>(const GuestSize&) const = default;

private:
    std::uint64_t value_;
};

class GuestAddress {
public:
    constexpr explicit GuestAddress(std::uint64_t value = 0) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] static astraea::core::Result<GuestAddress, AddressError> checked_add(
        GuestAddress base,
        GuestSize offset) {
        if (base.value_ > std::numeric_limits<std::uint64_t>::max() - offset.value()) {
            return astraea::core::Result<GuestAddress, AddressError>::failure(
                AddressError{AddressErrorCode::address_addition_overflow});
        }
        return astraea::core::Result<GuestAddress, AddressError>::success(
            GuestAddress{base.value_ + offset.value()});
    }

    auto operator<=>(const GuestAddress&) const = default;

private:
    std::uint64_t value_;
};

class GuestRange {
public:
    [[nodiscard]] static astraea::core::Result<GuestRange, AddressError> create(
        GuestAddress base,
        GuestSize size) {
        if (size.value() > 0) {
            const auto last_offset = size.value() - 1U;
            if (base.value() > std::numeric_limits<std::uint64_t>::max() - last_offset) {
                return astraea::core::Result<GuestRange, AddressError>::failure(
                    AddressError{AddressErrorCode::guest_range_overflow});
            }
        }
        return astraea::core::Result<GuestRange, AddressError>::success(
            GuestRange{base, size});
    }

    [[nodiscard]] constexpr GuestAddress base() const noexcept {
        return base_;
    }

    [[nodiscard]] constexpr GuestSize size() const noexcept {
        return size_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size_.value() == 0;
    }

    [[nodiscard]] constexpr bool contains(GuestAddress address) const noexcept {
        if (empty() || address.value() < base_.value()) {
            return false;
        }
        return address.value() - base_.value() < size_.value();
    }

    [[nodiscard]] constexpr bool overlaps(const GuestRange& other) const noexcept {
        if (empty() || other.empty()) {
            return false;
        }
        return contains(other.base_) || other.contains(base_);
    }

    auto operator<=>(const GuestRange&) const = default;

private:
    constexpr GuestRange(GuestAddress base, GuestSize size) noexcept
        : base_(base), size_(size) {}

    GuestAddress base_;
    GuestSize size_;
};

}  // namespace astraea::memory
