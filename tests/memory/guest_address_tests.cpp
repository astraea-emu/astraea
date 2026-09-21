#include <astraea/memory/guest_address.hpp>

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

using astraea::memory::AddressErrorCode;
using astraea::memory::GuestAddress;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;

TEST_CASE("GuestRange supports the full uint64 address domain", "[memory][range]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();

    auto last_byte = GuestRange::create(GuestAddress{max}, GuestSize{1});
    REQUIRE(last_byte.has_value());
    REQUIRE(last_byte->contains(GuestAddress{max}));

    auto overflow = GuestRange::create(GuestAddress{max}, GuestSize{2});
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code == AddressErrorCode::guest_range_overflow);
}

TEST_CASE("empty GuestRange contains and overlaps nothing", "[memory][range]") {
    auto empty = GuestRange::create(GuestAddress{0x1000}, GuestSize{0});
    auto nonempty = GuestRange::create(GuestAddress{0x1000}, GuestSize{1});

    REQUIRE(empty.has_value());
    REQUIRE(nonempty.has_value());
    REQUIRE(empty->empty());
    REQUIRE_FALSE(empty->contains(GuestAddress{0x1000}));
    REQUIRE_FALSE(empty->overlaps(nonempty.value()));
    REQUIRE_FALSE(nonempty->overlaps(empty.value()));
}

TEST_CASE("GuestRange containment handles boundaries without end overflow", "[memory][range]") {
    auto range = GuestRange::create(GuestAddress{0x1000}, GuestSize{0x100});

    REQUIRE(range.has_value());
    REQUIRE(range->contains(GuestAddress{0x1000}));
    REQUIRE(range->contains(GuestAddress{0x10ff}));
    REQUIRE_FALSE(range->contains(GuestAddress{0x0fff}));
    REQUIRE_FALSE(range->contains(GuestAddress{0x1100}));
}

TEST_CASE("GuestRange overlap distinguishes adjacency and intersection", "[memory][range]") {
    auto a = GuestRange::create(GuestAddress{0x1000}, GuestSize{0x100});
    auto adjacent = GuestRange::create(GuestAddress{0x1100}, GuestSize{0x100});
    auto partial = GuestRange::create(GuestAddress{0x1080}, GuestSize{0x100});
    auto contained = GuestRange::create(GuestAddress{0x1020}, GuestSize{0x10});
    auto identical = GuestRange::create(GuestAddress{0x1000}, GuestSize{0x100});

    REQUIRE(a.has_value());
    REQUIRE(adjacent.has_value());
    REQUIRE(partial.has_value());
    REQUIRE(contained.has_value());
    REQUIRE(identical.has_value());

    REQUIRE_FALSE(a->overlaps(adjacent.value()));
    REQUIRE(a->overlaps(partial.value()));
    REQUIRE(a->overlaps(contained.value()));
    REQUIRE(a->overlaps(identical.value()));
}

TEST_CASE("GuestAddress checked addition never wraps", "[memory][address]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();

    auto ok = GuestAddress::checked_add(GuestAddress{max - 1}, GuestSize{1});
    REQUIRE(ok.has_value());
    REQUIRE(ok->value() == max);

    auto overflow = GuestAddress::checked_add(GuestAddress{max}, GuestSize{1});
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code == AddressErrorCode::address_addition_overflow);
}

TEST_CASE("GuestSize checked arithmetic reports overflow and underflow", "[memory][size]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();

    auto sum = GuestSize::checked_add(GuestSize{5}, GuestSize{7});
    REQUIRE(sum.has_value());
    REQUIRE(sum->value() == 12);

    auto overflow = GuestSize::checked_add(GuestSize{max}, GuestSize{1});
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code == AddressErrorCode::size_addition_overflow);

    auto difference = GuestSize::checked_subtract(GuestSize{7}, GuestSize{5});
    REQUIRE(difference.has_value());
    REQUIRE(difference->value() == 2);

    auto underflow = GuestSize::checked_subtract(GuestSize{5}, GuestSize{7});
    REQUIRE_FALSE(underflow.has_value());
    REQUIRE(underflow.error().code == AddressErrorCode::size_subtraction_underflow);
}
