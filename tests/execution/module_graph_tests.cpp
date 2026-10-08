#include <astraea/execution/module_graph.hpp>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::ModuleGraph;
using astraea::execution::ModuleGraphDeclaration;
using astraea::execution::ModuleGraphErrorCode;
using astraea::execution::ModuleGraphExport;
using astraea::execution::ModuleGraphResolutionKind;

[[nodiscard]] astraea::loader::SceSymbolIdentity symbol(
    const char* nid = "ABCDEFGHIJK",
    const char* library = "library-a",
    const char* module = "module-a") {
    return astraea::loader::SceSymbolIdentity{
        .nid = nid,
        .library_id = library,
        .module_id = module,
    };
}

[[nodiscard]] std::vector<ModuleGraphDeclaration> owned_modules() {
    return {
        ModuleGraphDeclaration{
            .module_key = "owned-client",
            .dependency_keys = {"owned-provider", "not-yet-loaded"},
            .exports = {},
        },
        ModuleGraphDeclaration{
            .module_key = "owned-provider",
            .dependency_keys = {},
            .exports = {
                ModuleGraphExport{
                    .identity = symbol(),
                    .guest_address =
                        astraea::memory::GuestAddress{0x12345000U},
                },
            },
        },
        ModuleGraphDeclaration{
            .module_key = "unrelated-provider",
            .dependency_keys = {},
            .exports = {
                ModuleGraphExport{
                    .identity = symbol(),
                    .guest_address =
                        astraea::memory::GuestAddress{0x23456000U},
                },
            },
        },
    };
}

}  // namespace

TEST_CASE(
    "owned module graph resolves only explicit requester provider edge and exact identity",
    "[execution][module-graph]") {
    const auto modules = owned_modules();
    const auto result = ModuleGraph::create(modules);
    REQUIRE(result.has_value());
    REQUIRE(result->module_count() == 3);

    const auto resolved = result->resolve(
        "owned-client", "owned-provider", symbol());
    REQUIRE(resolved.kind == ModuleGraphResolutionKind::resolved);
    REQUIRE(resolved.guest_address ==
        astraea::memory::GuestAddress{0x12345000U});

    REQUIRE(result->resolve(
        "unknown-client", "owned-provider", symbol()).kind ==
        ModuleGraphResolutionKind::unknown_requester);

    REQUIRE(result->resolve(
        "owned-client", "unrelated-provider", symbol()).kind ==
        ModuleGraphResolutionKind::undeclared_dependency);

    REQUIRE(result->resolve(
        "owned-client", "not-yet-loaded", symbol()).kind ==
        ModuleGraphResolutionKind::provider_not_registered);

    REQUIRE(result->resolve(
        "owned-client", "owned-provider",
        symbol("ABCDEFGHIJK", "library-b", "module-a")).kind ==
        ModuleGraphResolutionKind::unresolved_export);

    REQUIRE(result->resolve(
        "owned-client", "owned-provider",
        symbol("short", "library-a", "module-a")).kind ==
        ModuleGraphResolutionKind::invalid_symbol_identity);
}

TEST_CASE(
    "unknown owned module export does not fabricate a guest address",
    "[execution][module-graph]") {
    const auto graph = ModuleGraph::create(owned_modules());
    REQUIRE(graph.has_value());
    const auto unresolved = graph->resolve(
        "owned-client", "owned-provider",
        symbol("LMNOPQRSTUV", "library-a", "module-a"));
    REQUIRE(unresolved.kind == ModuleGraphResolutionKind::unresolved_export);
    REQUIRE_FALSE(unresolved.guest_address.has_value());
}

TEST_CASE(
    "module graph rejects duplicate module keys and duplicate dependencies",
    "[execution][module-graph]") {
    auto modules = owned_modules();
    modules[1].module_key = "owned-client";
    auto duplicate_module = ModuleGraph::create(modules);
    REQUIRE_FALSE(duplicate_module.has_value());
    REQUIRE(duplicate_module.error().code ==
        ModuleGraphErrorCode::duplicate_module_key);
    REQUIRE(duplicate_module.error().module_index == 1);
    REQUIRE(duplicate_module.error().conflicting_index == 0U);

    modules = owned_modules();
    modules[0].dependency_keys.push_back("owned-provider");
    auto duplicate_edge = ModuleGraph::create(modules);
    REQUIRE_FALSE(duplicate_edge.has_value());
    REQUIRE(duplicate_edge.error().code ==
        ModuleGraphErrorCode::duplicate_dependency_key);
    REQUIRE(duplicate_edge.error().record_index == 2U);
    REQUIRE(duplicate_edge.error().conflicting_index == 0U);
}

TEST_CASE(
    "module graph refuses malformed and duplicate exports",
    "[execution][module-graph]") {
    auto modules = owned_modules();
    modules[1].exports[0].identity.nid = "short";
    auto invalid = ModuleGraph::create(modules);
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(invalid.error().code ==
        ModuleGraphErrorCode::invalid_export_identity);

    modules = owned_modules();
    modules[1].exports[0].guest_address =
        astraea::memory::GuestAddress{0U};
    auto null_address = ModuleGraph::create(modules);
    REQUIRE_FALSE(null_address.has_value());
    REQUIRE(null_address.error().code ==
        ModuleGraphErrorCode::invalid_export_address);

    modules = owned_modules();
    modules[1].exports.push_back(modules[1].exports[0]);
    auto duplicate = ModuleGraph::create(modules);
    REQUIRE_FALSE(duplicate.has_value());
    REQUIRE(duplicate.error().code ==
        ModuleGraphErrorCode::duplicate_export_identity);
    REQUIRE(duplicate.error().record_index == 1U);
    REQUIRE(duplicate.error().conflicting_index == 0U);
}

TEST_CASE(
    "module graph keeps identity exact across provider boundaries",
    "[execution][module-graph]") {
    auto modules = owned_modules();
    modules[0].dependency_keys.push_back("unrelated-provider");
    const auto graph = ModuleGraph::create(modules);
    REQUIRE(graph.has_value());
    const auto first = graph->resolve(
        "owned-client", "owned-provider", symbol());
    const auto second = graph->resolve(
        "owned-client", "unrelated-provider", symbol());
    REQUIRE(first.kind == ModuleGraphResolutionKind::resolved);
    REQUIRE(second.kind == ModuleGraphResolutionKind::resolved);
    REQUIRE(first.guest_address != second.guest_address);
}
