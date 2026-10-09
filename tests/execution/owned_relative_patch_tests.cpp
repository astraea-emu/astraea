#include <astraea/execution/owned_relative_patch.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::OwnedRelativePatchErrorCode;
using astraea::loader::RelocationTableKind;

[[nodiscard]] astraea::loader::DynamicRelocation relocation(
    std::int64_t addend = 0,
    std::uint32_t type = 8U,
    std::uint32_t symbol_index = 0U,
    RelocationTableKind kind = RelocationTableKind::rela) {
    return {
        .table_kind = kind,
        .table_index = 2U,
        .target = astraea::memory::GuestAddress{0x40001000U},
        .raw_info = (static_cast<std::uint64_t>(symbol_index) << 32U) | type,
        .symbol_index = symbol_index,
        .relocation_type = type,
        .addend = addend,
    };
}

}  // namespace

TEST_CASE(
    "owned relative relocation produces exact B plus A little-endian bytes",
    "[execution][owned-relative]") {
    const auto patch =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(-9),
            astraea::memory::GuestAddress{0x12340000U});
    REQUIRE(patch.has_value());
    REQUIRE(patch->relocated_value ==
        astraea::memory::GuestAddress{0x1233fff7U});
    REQUIRE(patch->raw_addend == -9);
    const std::array<std::byte, 8> expected{
        std::byte{0xf7}, std::byte{0xff}, std::byte{0x33}, std::byte{0x12},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    REQUIRE(patch->bytes == expected);

    const auto positive =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(15),
            astraea::memory::GuestAddress{0x1000U});
    REQUIRE(positive.has_value());
    REQUIRE(positive->relocated_value ==
        astraea::memory::GuestAddress{0x100fU});
}

TEST_CASE(
    "relative relocation safely handles minimum signed addend",
    "[execution][owned-relative][boundaries]") {
    const auto patch =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(std::numeric_limits<std::int64_t>::min()),
            astraea::memory::GuestAddress{0x8000000000000000ULL});
    REQUIRE(patch.has_value());
    REQUIRE(patch->relocated_value ==
        astraea::memory::GuestAddress{0U});
}

TEST_CASE(
    "owned relative relocation refuses wrapped B plus A",
    "[execution][owned-relative][boundaries]") {
    auto underflow =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(-1), astraea::memory::GuestAddress{0U});
    REQUIRE_FALSE(underflow.has_value());
    REQUIRE(underflow.error().code ==
        OwnedRelativePatchErrorCode::relocated_value_overflow);

    auto overflow =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(2), astraea::memory::GuestAddress{
                std::numeric_limits<std::uint64_t>::max() - 1U});
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code ==
        OwnedRelativePatchErrorCode::relocated_value_overflow);
}

TEST_CASE(
    "owned relative relocation requires RELA type eight and no symbol",
    "[execution][owned-relative][refusal]") {
    const auto base = astraea::memory::GuestAddress{0x2000U};
    auto wrong_kind =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(0, 8U, 0U, RelocationTableKind::plt_rela), base);
    REQUIRE_FALSE(wrong_kind.has_value());
    REQUIRE(wrong_kind.error().code ==
        OwnedRelativePatchErrorCode::unsupported_table_kind);

    auto wrong_type =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(0, 7U), base);
    REQUIRE_FALSE(wrong_type.has_value());
    REQUIRE(wrong_type.error().code ==
        OwnedRelativePatchErrorCode::unsupported_relocation_type);

    auto wrong_symbol =
        astraea::execution::build_owned_x86_64_relative_patch(
            relocation(0, 8U, 1U), base);
    REQUIRE_FALSE(wrong_symbol.has_value());
    REQUIRE(wrong_symbol.error().code ==
        OwnedRelativePatchErrorCode::symbol_index_not_zero);

    auto no_addend = relocation();
    no_addend.addend.reset();
    auto missing =
        astraea::execution::build_owned_x86_64_relative_patch(
            no_addend, base);
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().code ==
        OwnedRelativePatchErrorCode::missing_rela_addend);

    auto unsafe = relocation();
    unsafe.target = astraea::memory::GuestAddress{
        std::numeric_limits<std::uint64_t>::max() - 2U};
    auto bad_target =
        astraea::execution::build_owned_x86_64_relative_patch(
            unsafe, base);
    REQUIRE_FALSE(bad_target.has_value());
    REQUIRE(bad_target.error().code ==
        OwnedRelativePatchErrorCode::target_range_overflow);
}
