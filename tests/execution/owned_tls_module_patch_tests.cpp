#include <astraea/execution/owned_tls_module_patch.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::OwnedTlsModuleId;
using astraea::execution::OwnedTlsModulePatchErrorCode;
using astraea::loader::RelocationTableKind;

[[nodiscard]] astraea::loader::DynamicRelocation owned_relocation(
    std::uint32_t symbol_index = 0U,
    std::uint32_t type = 16U,
    RelocationTableKind kind = RelocationTableKind::rela) {
    return {
        .table_kind = kind,
        .table_index = 3U,
        .target = astraea::memory::GuestAddress{0x40005000U},
        .raw_info = (static_cast<std::uint64_t>(symbol_index) << 32U) | type,
        .symbol_index = symbol_index,
        .relocation_type = type,
        .addend = std::int64_t{-17},
    };
}

}  // namespace

TEST_CASE(
    "owned DTPMOD64 patches exact explicitly assigned runtime TLS module index",
    "[execution][owned-tls][dtpmod64]") {
    const auto result =
        astraea::execution::build_owned_x86_64_dtpmod64_patch(
            owned_relocation(), OwnedTlsModuleId{0x12345678U});
    REQUIRE(result.has_value());
    REQUIRE(result->target == astraea::memory::GuestAddress{0x40005000U});
    REQUIRE(result->symbol_index == 0U);
    REQUIRE(result->tls_module_id == OwnedTlsModuleId{0x12345678U});
    REQUIRE(result->raw_addend == -17);
    const std::array<std::byte, 8> expected{
        std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    REQUIRE(result->bytes == expected);
}

TEST_CASE(
    "DTPMOD64 retains referenced symbol index without inferring TLS provider",
    "[execution][owned-tls][dtpmod64]") {
    const auto result =
        astraea::execution::build_owned_x86_64_dtpmod64_patch(
            owned_relocation(19U), OwnedTlsModuleId{3U});
    REQUIRE(result.has_value());
    REQUIRE(result->symbol_index == 19U);
    REQUIRE(result->bytes[0] == std::byte{3U});
    REQUIRE(result->bytes[7] == std::byte{0U});
}

TEST_CASE(
    "owned DTPMOD64 requires explicit nonzero runtime TLS module index",
    "[execution][owned-tls][dtpmod64][refusal]") {
    const auto result =
        astraea::execution::build_owned_x86_64_dtpmod64_patch(
            owned_relocation(), OwnedTlsModuleId{0U});
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
        OwnedTlsModulePatchErrorCode::invalid_module_id);
}

TEST_CASE(
    "owned DTPMOD64 rejects other relocation classes, tables and missing addends",
    "[execution][owned-tls][dtpmod64][refusal]") {
    auto wrong_table =
        astraea::execution::build_owned_x86_64_dtpmod64_patch(
            owned_relocation(0U, 16U, RelocationTableKind::plt_rela),
            OwnedTlsModuleId{1U});
    REQUIRE_FALSE(wrong_table.has_value());
    REQUIRE(wrong_table.error().code ==
        OwnedTlsModulePatchErrorCode::unsupported_table_kind);

    auto wrong_type =
        astraea::execution::build_owned_x86_64_dtpmod64_patch(
            owned_relocation(0U, 8U), OwnedTlsModuleId{1U});
    REQUIRE_FALSE(wrong_type.has_value());
    REQUIRE(wrong_type.error().code ==
        OwnedTlsModulePatchErrorCode::unsupported_relocation_type);

    auto missing = owned_relocation();
    missing.addend.reset();
    auto no_addend =
        astraea::execution::build_owned_x86_64_dtpmod64_patch(
            missing, OwnedTlsModuleId{1U});
    REQUIRE_FALSE(no_addend.has_value());
    REQUIRE(no_addend.error().code ==
        OwnedTlsModulePatchErrorCode::missing_rela_addend);

    auto unsafe = owned_relocation();
    unsafe.target = astraea::memory::GuestAddress{
        std::numeric_limits<std::uint64_t>::max() - 3U};
    auto overflow =
        astraea::execution::build_owned_x86_64_dtpmod64_patch(
            unsafe, OwnedTlsModuleId{1U});
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code ==
        OwnedTlsModulePatchErrorCode::target_range_overflow);
}
