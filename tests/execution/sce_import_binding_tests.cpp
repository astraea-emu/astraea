#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_import_binding.hpp>
#include <astraea/loader/sce_symbol_identity.hpp>

#include <array>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::execution::HleRegistry make_hle_registry() {
    auto result =
        astraea::execution::HleRegistry::create(
            std::vector<
                astraea::execution::HleFunctionDescriptor>{
                {
                    .id =
                        astraea::execution::HleFunctionId{1},
                    .canonical_name =
                        "synthetic.service.one",
                    .argument_count = 0,
                },
                {
                    .id =
                        astraea::execution::HleFunctionId{2},
                    .canonical_name =
                        "synthetic.service.two",
                    .argument_count = 0,
                },
            });

    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::loader::SceSymbolIdentity identity(
    const char* nid,
    const char* library_id,
    const char* module_id) {
    return astraea::loader::SceSymbolIdentity{
        .nid = nid,
        .library_id = library_id,
        .module_id = module_id,
    };
}

}  // namespace

TEST_CASE(
    "SCE import binding lookup requires exact opaque identity",
    "[execution][sce-import-binding]") {
    const auto hle = make_hle_registry();
    const std::array bindings{
        astraea::execution::SceImportBinding{
            .identity =
                identity(
                    "ABCDEFGHIJK",
                    "library-a",
                    "module-a"),
            .function_id =
                astraea::execution::HleFunctionId{1},
        },
        astraea::execution::SceImportBinding{
            .identity =
                identity(
                    "ABCDEFGHIJK",
                    "library-b",
                    "module-a"),
            .function_id =
                astraea::execution::HleFunctionId{2},
        },
    };

    const auto registry =
        astraea::execution::
            SceImportBindingRegistry::create(
                hle,
                bindings);

    REQUIRE(registry.has_value());

    const auto first =
        registry->resolve(
            identity(
                "ABCDEFGHIJK",
                "library-a",
                "module-a"));
    REQUIRE(
        first.kind ==
        astraea::execution::
            SceImportResolutionKind::resolved);
    REQUIRE(
        first.function_id ==
        astraea::execution::HleFunctionId{1});

    const auto second =
        registry->resolve(
            identity(
                "ABCDEFGHIJK",
                "library-b",
                "module-a"));
    REQUIRE(
        second.kind ==
        astraea::execution::
            SceImportResolutionKind::resolved);
    REQUIRE(
        second.function_id ==
        astraea::execution::HleFunctionId{2});

    const auto unresolved =
        registry->resolve(
            identity(
                "ABCDEFGHIJK",
                "library-a",
                "module-b"));
    REQUIRE(
        unresolved.kind ==
        astraea::execution::
            SceImportResolutionKind::unresolved);
    REQUIRE_FALSE(
        unresolved.function_id.has_value());
}

TEST_CASE(
    "multiple exact SCE identities may bind the same HLE function",
    "[execution][sce-import-binding]") {
    const auto hle = make_hle_registry();
    const std::array bindings{
        astraea::execution::SceImportBinding{
            .identity =
                identity(
                    "ABCDEFGHIJK",
                    "library-a",
                    "module-a"),
            .function_id =
                astraea::execution::HleFunctionId{1},
        },
        astraea::execution::SceImportBinding{
            .identity =
                identity(
                    "LMNOPQRSTUV",
                    "library-b",
                    "module-b"),
            .function_id =
                astraea::execution::HleFunctionId{1},
        },
    };

    const auto registry =
        astraea::execution::
            SceImportBindingRegistry::create(
                hle,
                bindings);

    REQUIRE(registry.has_value());
    REQUIRE(registry->bindings().size() == 2);
}

TEST_CASE(
    "duplicate exact SCE import identity is rejected",
    "[execution][sce-import-binding]") {
    const auto hle = make_hle_registry();
    const std::array bindings{
        astraea::execution::SceImportBinding{
            .identity =
                identity(
                    "ABCDEFGHIJK",
                    "library-a",
                    "module-a"),
            .function_id =
                astraea::execution::HleFunctionId{1},
        },
        astraea::execution::SceImportBinding{
            .identity =
                identity(
                    "ABCDEFGHIJK",
                    "library-a",
                    "module-a"),
            .function_id =
                astraea::execution::HleFunctionId{2},
        },
    };

    const auto registry =
        astraea::execution::
            SceImportBindingRegistry::create(
                hle,
                bindings);

    REQUIRE_FALSE(registry.has_value());
    REQUIRE(
        registry.error().code ==
        astraea::execution::
            SceImportBindingErrorCode::
                duplicate_identity);
    REQUIRE(
        registry.error().binding_index ==
        std::size_t{1});
    REQUIRE(
        registry.error().conflicting_binding_index ==
        std::size_t{0});
}

TEST_CASE(
    "SCE import binding rejects unknown HLE function",
    "[execution][sce-import-binding]") {
    const auto hle = make_hle_registry();
    const std::array bindings{
        astraea::execution::SceImportBinding{
            .identity =
                identity(
                    "ABCDEFGHIJK",
                    "library-a",
                    "module-a"),
            .function_id =
                astraea::execution::HleFunctionId{99},
        },
    };

    const auto registry =
        astraea::execution::
            SceImportBindingRegistry::create(
                hle,
                bindings);

    REQUIRE_FALSE(registry.has_value());
    REQUIRE(
        registry.error().code ==
        astraea::execution::
            SceImportBindingErrorCode::
                unknown_hle_function);
    REQUIRE(
        registry.error().binding_index ==
        std::size_t{0});
    REQUIRE(
        registry.error().function_id ==
        astraea::execution::HleFunctionId{99});
}

TEST_CASE(
    "SCE import binding validates opaque identity structure",
    "[execution][sce-import-binding]") {
    const auto hle = make_hle_registry();

    SECTION("NID length") {
        const std::array bindings{
            astraea::execution::SceImportBinding{
                .identity =
                    identity(
                        "ABCDEFGHIJ",
                        "library-a",
                        "module-a"),
                .function_id =
                    astraea::execution::HleFunctionId{1},
            },
        };

        const auto registry =
            astraea::execution::
                SceImportBindingRegistry::create(
                    hle,
                    bindings);

        REQUIRE_FALSE(registry.has_value());
        REQUIRE(
            registry.error().code ==
            astraea::execution::
                SceImportBindingErrorCode::
                    invalid_identity);
    }

    SECTION("empty library") {
        const std::array bindings{
            astraea::execution::SceImportBinding{
                .identity =
                    identity(
                        "ABCDEFGHIJK",
                        "",
                        "module-a"),
                .function_id =
                    astraea::execution::HleFunctionId{1},
            },
        };

        const auto registry =
            astraea::execution::
                SceImportBindingRegistry::create(
                    hle,
                    bindings);

        REQUIRE_FALSE(registry.has_value());
        REQUIRE(
            registry.error().code ==
            astraea::execution::
                SceImportBindingErrorCode::
                    invalid_identity);
    }

    SECTION("empty module") {
        const std::array bindings{
            astraea::execution::SceImportBinding{
                .identity =
                    identity(
                        "ABCDEFGHIJK",
                        "library-a",
                        ""),
                .function_id =
                    astraea::execution::HleFunctionId{1},
            },
        };

        const auto registry =
            astraea::execution::
                SceImportBindingRegistry::create(
                    hle,
                    bindings);

        REQUIRE_FALSE(registry.has_value());
        REQUIRE(
            registry.error().code ==
            astraea::execution::
                SceImportBindingErrorCode::
                    invalid_identity);
    }
}
