#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_import_binding.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/loader/sce_symbol_identity.hpp>

#include <array>
#include <cstdint>
#include <string>
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
            });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::loader::SceSymbolIdentity make_identity(
    const char* nid = "ABCDEFGHIJK",
    const char* library_id = "library-a",
    const char* module_id = "module-a") {
    return astraea::loader::SceSymbolIdentity{
        .nid = nid,
        .library_id = library_id,
        .module_id = module_id,
    };
}

astraea::execution::SceImportBindingRegistry
make_binding_registry(
    const astraea::execution::HleRegistry& hle) {
    const std::array bindings{
        astraea::execution::SceImportBinding{
            .identity = make_identity(),
            .function_id =
                astraea::execution::HleFunctionId{1},
        },
    };

    auto result =
        astraea::execution::
            SceImportBindingRegistry::create(
                hle,
                bindings);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::loader::DynamicRelocation make_relocation(
    std::uint32_t symbol_index = 3) {
    return astraea::loader::DynamicRelocation{
        .table_kind =
            astraea::loader::RelocationTableKind::
                plt_rela,
        .table_index = 7,
        .target =
            astraea::memory::GuestAddress{0x12345000},
        .raw_info = 0xabcdef0123456789ULL,
        .symbol_index = symbol_index,
        .relocation_type = 0xfeedbeefU,
        .addend = std::int64_t{-11},
    };
}

astraea::loader::SceDynamicSymbolRecord make_symbol(
    astraea::loader::SceSymbolIdentity identity =
        make_identity(),
    std::uint64_t symbol_index = 3,
    std::uint16_t section_index = 0) {
    return astraea::loader::SceDynamicSymbolRecord{
        .symbol =
            astraea::loader::DynamicSymbol{
                .index = symbol_index,
                .name_offset = 9,
                .info = 0x12,
                .other = 0,
                .section_index_raw = section_index,
                .value = 0,
                .size = 0,
            },
        .name =
            astraea::loader::SceDynamicSymbolName{
                .raw =
                    "ABCDEFGHIJK#library-a#module-a",
                .identity = std::move(identity),
            },
    };
}

}  // namespace

TEST_CASE(
    "SCE import relocation plan preserves validated relocation and identity evidence",
    "[execution][sce-import-resolution]") {
    const auto hle = make_hle_registry();
    const auto bindings =
        make_binding_registry(hle);
    const auto relocation = make_relocation();
    const auto symbol = make_symbol();

    const auto result =
        astraea::execution::
            plan_sce_import_resolution(
                relocation,
                symbol,
                bindings);

    REQUIRE(result.has_value());
    REQUIRE(
        result->table_kind ==
        astraea::loader::RelocationTableKind::
            plt_rela);
    REQUIRE(result->table_index == 7);
    REQUIRE(
        result->relocation_target ==
        astraea::memory::GuestAddress{
            0x12345000});
    REQUIRE(
        result->raw_relocation_type ==
        0xfeedbeefU);
    REQUIRE(
        result->raw_addend ==
        std::optional<std::int64_t>{-11});
    REQUIRE(result->symbol_index == 3);
    REQUIRE(
        result->raw_symbol_name ==
        "ABCDEFGHIJK#library-a#module-a");
    REQUIRE(result->identity == make_identity());
    REQUIRE(
        result->function_id ==
        astraea::execution::HleFunctionId{1});
}

TEST_CASE(
    "SCE import relocation requires exact symbol index agreement",
    "[execution][sce-import-resolution]") {
    const auto hle = make_hle_registry();
    const auto bindings =
        make_binding_registry(hle);

    const auto result =
        astraea::execution::
            plan_sce_import_resolution(
                make_relocation(4),
                make_symbol(),
                bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceImportResolutionPlanErrorCode::
                symbol_index_mismatch);
    REQUIRE(
        result.error().relocation_symbol_index ==
        4);
    REQUIRE(
        result.error().symbol_record_index ==
        3);
}

TEST_CASE(
    "defined ELF symbol is not treated as an SCE import",
    "[execution][sce-import-resolution]") {
    const auto hle = make_hle_registry();
    const auto bindings =
        make_binding_registry(hle);

    const auto result =
        astraea::execution::
            plan_sce_import_resolution(
                make_relocation(),
                make_symbol(
                    make_identity(),
                    3,
                    1),
                bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceImportResolutionPlanErrorCode::
                symbol_not_undefined);
}

TEST_CASE(
    "plain dynamic symbol name is not guessed into an SCE identity",
    "[execution][sce-import-resolution]") {
    const auto hle = make_hle_registry();
    const auto bindings =
        make_binding_registry(hle);

    auto symbol = make_symbol();
    symbol.name.raw = "plain_symbol";
    symbol.name.identity = std::nullopt;

    const auto result =
        astraea::execution::
            plan_sce_import_resolution(
                make_relocation(),
                symbol,
                bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceImportResolutionPlanErrorCode::
                missing_sce_identity);
}

TEST_CASE(
    "unconfigured exact SCE identity remains unresolved",
    "[execution][sce-import-resolution]") {
    const auto hle = make_hle_registry();
    const auto bindings =
        make_binding_registry(hle);

    const auto result =
        astraea::execution::
            plan_sce_import_resolution(
                make_relocation(),
                make_symbol(
                    make_identity(
                        "ABCDEFGHIJK",
                        "library-b",
                        "module-a")),
                bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceImportResolutionPlanErrorCode::
                unresolved_import);
}

TEST_CASE(
    "same NID in different module namespace does not resolve accidentally",
    "[execution][sce-import-resolution]") {
    const auto hle = make_hle_registry();
    const auto bindings =
        make_binding_registry(hle);

    const auto result =
        astraea::execution::
            plan_sce_import_resolution(
                make_relocation(),
                make_symbol(
                    make_identity(
                        "ABCDEFGHIJK",
                        "library-a",
                        "module-b")),
                bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceImportResolutionPlanErrorCode::
                unresolved_import);
}
