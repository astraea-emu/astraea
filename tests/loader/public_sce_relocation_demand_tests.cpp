#include <astraea/loader/public_sce_relocation_demand.hpp>

#include <cstdint>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::PublicSceDemandClass;
using astraea::loader::PublicSceDemandImport;
using astraea::loader::PublicSceDemandReference;
using astraea::loader::PublicSceRelocationDemandLedger;

[[nodiscard]] PublicSceDemandReference imported(
    std::uint32_t symbol_index,
    std::uint32_t relocation_type,
    std::uint16_t module = 2U,
    std::uint16_t library = 1U,
    const char* nid = "ABCDEFGHIJK") {
    return PublicSceDemandReference{
        .symbol_index = symbol_index,
        .relocation_type = relocation_type,
        .symbol_is_undefined = true,
        .public_import = PublicSceDemandImport{
            .module_local_id = module,
            .library_local_id = library,
            .nid = nid,
        },
    };
}

}  // namespace

TEST_CASE(
    "public relocation ledger distinguishes null local and unresolved references",
    "[loader][public-sce-demand]") {
    PublicSceRelocationDemandLedger ledger;
    REQUIRE(ledger.record(PublicSceDemandReference{
        .symbol_index = 0U,
        .relocation_type = 8U,
        .symbol_is_undefined = true,
        .public_import = std::nullopt,
    }) == PublicSceDemandClass::null_symbol);
    REQUIRE(ledger.record(PublicSceDemandReference{
        .symbol_index = 1U,
        .relocation_type = 6U,
        .symbol_is_undefined = false,
        .public_import = std::nullopt,
    }) == PublicSceDemandClass::local_defined_symbol);
    REQUIRE(ledger.record(PublicSceDemandReference{
        .symbol_index = 2U,
        .relocation_type = 7U,
        .symbol_is_undefined = true,
        .public_import = std::nullopt,
    }) == PublicSceDemandClass::external_unclassified);
    const auto counts = ledger.counts();
    REQUIRE(counts.total == 3U);
    REQUIRE(counts.null_symbol == 1U);
    REQUIRE(counts.local_defined == 1U);
    REQUIRE(counts.external_unclassified == 1U);
    REQUIRE(counts.public_external_import == 0U);
    REQUIRE(counts.unique_imported_symbol_indices == 0U);
    REQUIRE(counts.unique_public_import_identities == 0U);
    REQUIRE(ledger.groups().empty());
}

TEST_CASE(
    "public relocation demand groups are keyed by both local IDs and numeric type",
    "[loader][public-sce-demand][group]") {
    PublicSceRelocationDemandLedger ledger;
    REQUIRE(ledger.record(imported(3U, 6U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(3U, 6U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(3U, 7U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(4U, 6U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(5U, 6U, 3U, 1U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(6U, 6U, 2U, 2U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(7U, 6U, 2U, 1U, "LMNOPQRSTUV")) ==
        PublicSceDemandClass::public_external_import);
    const auto counts = ledger.counts();
    REQUIRE(counts.total == 7U);
    REQUIRE(counts.public_external_import == 7U);
    REQUIRE(counts.unique_imported_symbol_indices == 5U);
    REQUIRE(counts.unique_public_import_identities == 4U);
    REQUIRE(ledger.groups().size() == 4U);
    REQUIRE(ledger.groups().at(std::tuple{2U, 1U, 6U}) == 4U);
    REQUIRE(ledger.groups().at(std::tuple{2U, 1U, 7U}) == 1U);
    REQUIRE(ledger.groups().at(std::tuple{3U, 1U, 6U}) == 1U);
    REQUIRE(ledger.groups().at(std::tuple{2U, 2U, 6U}) == 1U);
}

TEST_CASE(
    "same NID from different public module IDs is not conflated",
    "[loader][public-sce-demand][identity]") {
    PublicSceRelocationDemandLedger ledger;
    REQUIRE(ledger.record(imported(3U, 7U, 2U, 1U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(4U, 7U, 3U, 1U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.record(imported(5U, 7U, 2U, 2U)) ==
        PublicSceDemandClass::public_external_import);
    REQUIRE(ledger.counts().unique_public_import_identities == 3U);
    REQUIRE(ledger.groups().size() == 3U);
}
