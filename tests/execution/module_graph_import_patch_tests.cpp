#include <astraea/execution/module_graph_import_patch.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::ModuleGraphImportPlan;
using astraea::execution::OwnedModuleAbsolutePatchErrorCode;
using astraea::loader::RelocationTableKind;

[[nodiscard]] ModuleGraphImportPlan plan(
    std::uint32_t type,
    RelocationTableKind kind) {
    return ModuleGraphImportPlan{
        .table_kind = kind,
        .table_index = 2U,
        .relocation_target = astraea::memory::GuestAddress{0x40001000U},
        .raw_relocation_type = type,
        .raw_addend = std::int64_t{13},
        .symbol_index = 5U,
        .symbol_binding = 1U,
        .provider_key = "owned-provider",
        .symbol_identity = {
            .nid = "ABCDEFGHIJK",
            .library_id = "test-library",
            .module_id = "test-module",
        },
        .provider_guest_address =
            astraea::memory::GuestAddress{0x1234567890abcdefULL},
    };
}

}  // namespace

TEST_CASE(
    "owned GLOB_DAT encodes exact provider guest address and preserves addend",
    "[execution][module-graph][import-patch]") {
    const auto p = plan(6U, RelocationTableKind::rela);
    const auto result =
        astraea::execution::build_owned_x86_64_module_import_patch(p);
    REQUIRE(result.has_value());
    REQUIRE(result->target == p.relocation_target);
    REQUIRE(result->source_symbol_address == p.provider_guest_address);
    REQUIRE(result->raw_addend == p.raw_addend);
    REQUIRE(result->raw_relocation_type == 6U);
    const std::array<std::byte, 8> expected{
        std::byte{0xef}, std::byte{0xcd}, std::byte{0xab}, std::byte{0x90},
        std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
    };
    REQUIRE(result->bytes == expected);
}

TEST_CASE(
    "owned JUMP_SLOT emits same S value only for PLT RELA",
    "[execution][module-graph][import-patch]") {
    const auto result =
        astraea::execution::build_owned_x86_64_module_import_patch(
            plan(7U, RelocationTableKind::plt_rela));
    REQUIRE(result.has_value());
    REQUIRE(result->bytes[0] == std::byte{0xef});
    REQUIRE(result->bytes[7] == std::byte{0x12});
}

TEST_CASE(
    "owned patch refuses unsupported relocation kind or table",
    "[execution][module-graph][import-patch]") {
    for (const auto type : {1U, 8U, 0U, 42U}) {
        const auto result =
            astraea::execution::build_owned_x86_64_module_import_patch(
                plan(type, RelocationTableKind::rela));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code ==
            OwnedModuleAbsolutePatchErrorCode::unsupported_relocation_type);
    }

    for (const auto [type, kind] :
         {std::pair{6U, RelocationTableKind::plt_rela},
          std::pair{7U, RelocationTableKind::rela},
          std::pair{6U, RelocationTableKind::rel},
          std::pair{7U, RelocationTableKind::plt_rel}}) {
        const auto result =
            astraea::execution::build_owned_x86_64_module_import_patch(
                plan(type, kind));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code ==
            OwnedModuleAbsolutePatchErrorCode::unsupported_table_kind);
    }
}

TEST_CASE(
    "owned absolute import patch refuses unsafe target or missing evidence",
    "[execution][module-graph][import-patch]") {
    auto p = plan(6U, RelocationTableKind::rela);
    p.raw_addend.reset();
    auto result =
        astraea::execution::build_owned_x86_64_module_import_patch(p);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
        OwnedModuleAbsolutePatchErrorCode::missing_rela_addend);

    p = plan(6U, RelocationTableKind::rela);
    p.provider_guest_address = astraea::memory::GuestAddress{0U};
    result = astraea::execution::build_owned_x86_64_module_import_patch(p);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
        OwnedModuleAbsolutePatchErrorCode::invalid_export_address);

    p = plan(6U, RelocationTableKind::rela);
    p.relocation_target = astraea::memory::GuestAddress{
        std::numeric_limits<std::uint64_t>::max() - 2U};
    result = astraea::execution::build_owned_x86_64_module_import_patch(p);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
        OwnedModuleAbsolutePatchErrorCode::target_range_overflow);
}
