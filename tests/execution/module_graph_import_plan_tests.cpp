#include <astraea/execution/module_graph_import_plan.hpp>

#include <array>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::ModuleGraph;
using astraea::execution::ModuleGraphDeclaration;
using astraea::execution::ModuleGraphExport;
using astraea::execution::ModuleGraphImportPlanErrorCode;
using astraea::execution::ModuleGraphResolutionKind;

[[nodiscard]] astraea::loader::SceSymbolIdentity identity(
    const char* nid = "ABCDEFGHIJK",
    const char* lib = "independent-lib",
    const char* mod = "independent-module") {
    return {
        .nid = nid,
        .library_id = lib,
        .module_id = mod,
    };
}

[[nodiscard]] ModuleGraph make_graph() {
    const std::array declarations{
        ModuleGraphDeclaration{
            .module_key = "client",
            .dependency_keys = {"provider", "missing-provider"},
            .exports = {},
        },
        ModuleGraphDeclaration{
            .module_key = "provider",
            .dependency_keys = {},
            .exports = {
                ModuleGraphExport{
                    .identity = identity(),
                    .guest_address =
                        astraea::memory::GuestAddress{0x12340000U},
                },
            },
        },
    };
    auto result = ModuleGraph::create(declarations);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

[[nodiscard]] astraea::loader::DynamicRelocation relocation(
    std::uint32_t index = 5U) {
    return {
        .table_kind = astraea::loader::RelocationTableKind::rela,
        .table_index = 9U,
        .target = astraea::memory::GuestAddress{0x240000U},
        .raw_info = 0x500000007ULL,
        .symbol_index = index,
        .relocation_type = 7U,
        .addend = std::int64_t{-2},
    };
}

[[nodiscard]] astraea::loader::SceDynamicSymbolRecord symbol(
    std::uint64_t index = 5U,
    std::uint16_t section = 0U) {
    return {
        .symbol = astraea::loader::DynamicSymbol{
            .index = index,
            .name_offset = 16U,
            .info = 0x12U,
            .other = 0U,
            .section_index_raw = section,
            .value = 0U,
            .size = 0U,
        },
        .name = astraea::loader::SceDynamicSymbolName{
            .raw = "ABCDEFGHIJK#independent-lib#independent-module",
            .identity = identity(),
        },
    };
}

}  // namespace

TEST_CASE(
    "owned relocation associates exact symbol to explicitly declared provider",
    "[execution][module-graph][import-plan]") {
    const auto graph = make_graph();
    const auto result = astraea::execution::plan_module_graph_import(
        relocation(), symbol(), graph, "client", "provider");

    REQUIRE(result.has_value());
    REQUIRE(result->table_kind ==
        astraea::loader::RelocationTableKind::rela);
    REQUIRE(result->table_index == 9U);
    REQUIRE(result->relocation_target ==
        astraea::memory::GuestAddress{0x240000U});
    REQUIRE(result->raw_relocation_type == 7U);
    REQUIRE(result->raw_addend == std::optional<std::int64_t>{-2});
    REQUIRE(result->symbol_index == 5U);
    REQUIRE(result->symbol_binding == 1U);
    REQUIRE(result->provider_key == "provider");
    REQUIRE(result->symbol_identity == identity());
    REQUIRE(result->provider_guest_address ==
        astraea::memory::GuestAddress{0x12340000U});
}

TEST_CASE(
    "module import planner refuses mismatched, defined and unnamed symbols",
    "[execution][module-graph][import-plan]") {
    const auto graph = make_graph();
    auto mismatch = astraea::execution::plan_module_graph_import(
        relocation(6U), symbol(), graph, "client", "provider");
    REQUIRE_FALSE(mismatch.has_value());
    REQUIRE(mismatch.error().code ==
        ModuleGraphImportPlanErrorCode::symbol_index_mismatch);
    REQUIRE(mismatch.error().relocation_symbol_index == 6U);
    REQUIRE(mismatch.error().dynamic_symbol_record_index == 5U);

    auto defined = astraea::execution::plan_module_graph_import(
        relocation(), symbol(5U, 1U), graph, "client", "provider");
    REQUIRE_FALSE(defined.has_value());
    REQUIRE(defined.error().code ==
        ModuleGraphImportPlanErrorCode::symbol_not_undefined);

    auto unnamed = symbol();
    unnamed.name.identity.reset();
    auto unresolved = astraea::execution::plan_module_graph_import(
        relocation(), unnamed, graph, "client", "provider");
    REQUIRE_FALSE(unresolved.has_value());
    REQUIRE(unresolved.error().code ==
        ModuleGraphImportPlanErrorCode::missing_sce_identity);
}

TEST_CASE(
    "module import planner preserves exact graph failure without guest address",
    "[execution][module-graph][import-plan]") {
    const auto graph = make_graph();
    auto unknown_client = astraea::execution::plan_module_graph_import(
        relocation(), symbol(), graph, "unknown", "provider");
    REQUIRE_FALSE(unknown_client.has_value());
    REQUIRE(unknown_client.error().code ==
        ModuleGraphImportPlanErrorCode::module_resolution_failure);
    REQUIRE(unknown_client.error().resolution_failure ==
        ModuleGraphResolutionKind::unknown_requester);

    auto undeclared = astraea::execution::plan_module_graph_import(
        relocation(), symbol(), graph, "client", "unrelated");
    REQUIRE_FALSE(undeclared.has_value());
    REQUIRE(undeclared.error().resolution_failure ==
        ModuleGraphResolutionKind::undeclared_dependency);

    auto no_provider = astraea::execution::plan_module_graph_import(
        relocation(), symbol(), graph, "client", "missing-provider");
    REQUIRE_FALSE(no_provider.has_value());
    REQUIRE(no_provider.error().resolution_failure ==
        ModuleGraphResolutionKind::provider_not_registered);

    auto another_symbol = symbol();
    another_symbol.name.identity = identity("LMNOPQRSTUV");
    auto no_export = astraea::execution::plan_module_graph_import(
        relocation(), another_symbol, graph, "client", "provider");
    REQUIRE_FALSE(no_export.has_value());
    REQUIRE(no_export.error().resolution_failure ==
        ModuleGraphResolutionKind::unresolved_export);
}

TEST_CASE(
    "planning weak owned imports does not invent weak binding semantics",
    "[execution][module-graph][import-plan]") {
    const auto graph = make_graph();
    auto weak = symbol();
    weak.symbol.info = 0x22U;
    auto result = astraea::execution::plan_module_graph_import(
        relocation(), weak, graph, "client", "provider");
    REQUIRE(result.has_value());
    REQUIRE(result->symbol_binding == 2U);
    // No application or weak fallback is provided by the plan API.
}
