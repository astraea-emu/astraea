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

    for (const auto binding : {0U, 3U, 15U}) {
        p = plan(6U, RelocationTableKind::rela);
        p.symbol_binding = static_cast<std::uint8_t>(binding);
        result = astraea::execution::build_owned_x86_64_module_import_patch(p);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code ==
            OwnedModuleAbsolutePatchErrorCode::unsupported_symbol_binding);
    }

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

TEST_CASE(
    "owned graph to relocation association to patch bytes composes without writes",
    "[execution][module-graph][import-patch][composition]") {
    using astraea::execution::ModuleGraph;
    using astraea::execution::ModuleGraphDeclaration;
    using astraea::execution::ModuleGraphExport;

    const std::array declarations{
        ModuleGraphDeclaration{
            .module_key = "client",
            .dependency_keys = {"provider"},
            .exports = {},
        },
        ModuleGraphDeclaration{
            .module_key = "provider",
            .dependency_keys = {},
            .exports = {
                ModuleGraphExport{
                    .identity = {
                        .nid = "ABCDEFGHIJK",
                        .library_id = "test-library",
                        .module_id = "test-module",
                    },
                    .guest_address = astraea::memory::GuestAddress{
                        0x1234567890abcdefULL},
                },
            },
        },
    };
    const auto graph = ModuleGraph::create(declarations);
    REQUIRE(graph.has_value());

    const astraea::loader::DynamicRelocation relocation{
        .table_kind = RelocationTableKind::plt_rela,
        .table_index = 2U,
        .target = astraea::memory::GuestAddress{0x40001000U},
        .raw_info = 0x500000007ULL,
        .symbol_index = 5U,
        .relocation_type = 7U,
        .addend = std::int64_t{13},
    };
    const astraea::loader::SceDynamicSymbolRecord symbol{
        .symbol = {
            .index = 5U,
            .name_offset = 3U,
            .info = 0x12U,
            .other = 0U,
            .section_index_raw = 0U,
            .value = 0U,
            .size = 0U,
        },
        .name = {
            .raw = "ABCDEFGHIJK#test-library#test-module",
            .identity = astraea::loader::SceSymbolIdentity{
                .nid = "ABCDEFGHIJK",
                .library_id = "test-library",
                .module_id = "test-module",
            },
        },
    };
    const auto association =
        astraea::execution::plan_module_graph_import(
            relocation, symbol, graph.value(), "client", "provider");
    REQUIRE(association.has_value());
    REQUIRE(association->symbol_binding == 1U);

    const auto encoded =
        astraea::execution::build_owned_x86_64_module_import_patch(
            association.value());
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->source_symbol_address ==
        astraea::memory::GuestAddress{0x1234567890abcdefULL});
    REQUIRE(encoded->raw_addend == std::optional<std::int64_t>{13});
    const std::array<std::byte, 8> expected{
        std::byte{0xef}, std::byte{0xcd}, std::byte{0xab}, std::byte{0x90},
        std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
    };
    REQUIRE(encoded->bytes == expected);
}
