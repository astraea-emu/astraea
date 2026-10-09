#include <astraea/loader/public_sce_packed_fields.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "independent public SCE packed metadata splits local id version and name offset",
    "[loader][public-sce-packing]") {
    const auto word =
        astraea::loader::decode_public_sce_packed_fields(
            0x0001000100000018ULL);
    REQUIRE(word.local_id == 1U);
    REQUIRE(word.version == 1U);
    REQUIRE(word.name_offset == 24U);

    const auto first_library =
        astraea::loader::decode_public_sce_packed_fields(
            0x0000000100000018ULL);
    REQUIRE(first_library.local_id == 0U);
    REQUIRE(first_library.version == 1U);
    REQUIRE(first_library.name_offset == 24U);
}

TEST_CASE(
    "public SCE packed field extraction retains exact bit boundaries",
    "[loader][public-sce-packing]") {
    const auto max =
        astraea::loader::decode_public_sce_packed_fields(
            0xffffffffffffffffULL);
    REQUIRE(max.local_id == 0xffffU);
    REQUIRE(max.version == 0xffffU);
    REQUIRE(max.name_offset == 0xffffffffU);

    const auto zero =
        astraea::loader::decode_public_sce_packed_fields(0ULL);
    REQUIRE(zero.local_id == 0U);
    REQUIRE(zero.version == 0U);
    REQUIRE(zero.name_offset == 0U);

    const auto mixed =
        astraea::loader::decode_public_sce_packed_fields(
            0x1234abcd98765432ULL);
    REQUIRE(mixed.local_id == 0x1234U);
    REQUIRE(mixed.version == 0xabcdU);
    REQUIRE(mixed.name_offset == 0x98765432U);
}

TEST_CASE(
    "public SCE local IDs reject contradictory name and version reuse",
    "[loader][public-sce-packing][conflict]") {
    astraea::loader::PublicSceLocalIdLedger ledger;
    const astraea::loader::PublicScePackedFields first{
        .name_offset = 1U,
        .version = 1U,
        .local_id = 3U,
    };
    REQUIRE(ledger.record(first, "owned-provider") ==
        astraea::loader::PublicSceLocalIdRecordResult::inserted);
    REQUIRE(ledger.record(first, "owned-provider") ==
        astraea::loader::PublicSceLocalIdRecordResult::
            equivalent_duplicate);
    REQUIRE(ledger.size() == 1U);

    const auto same_id_new_name = first;
    REQUIRE(ledger.record(same_id_new_name, "different-provider") ==
        astraea::loader::PublicSceLocalIdRecordResult::
            conflicting_reuse);

    auto same_id_new_version = first;
    same_id_new_version.version = 2U;
    REQUIRE(ledger.record(same_id_new_version, "owned-provider") ==
        astraea::loader::PublicSceLocalIdRecordResult::
            conflicting_reuse);
    REQUIRE(ledger.size() == 1U);

    auto different_id = first;
    different_id.local_id = 4U;
    REQUIRE(ledger.record(different_id, "owned-provider") ==
        astraea::loader::PublicSceLocalIdRecordResult::inserted);
    REQUIRE(ledger.size() == 2U);
}

TEST_CASE(
    "independent public SCE local ID namespaces may share an ID",
    "[loader][public-sce-packing][namespace]") {
    astraea::loader::PublicSceLocalIdLedger modules;
    astraea::loader::PublicSceLocalIdLedger libraries;
    const astraea::loader::PublicScePackedFields shared{
        .name_offset = 17U,
        .version = 1U,
        .local_id = 0U,
    };
    REQUIRE(modules.record(shared, "published-module") ==
        astraea::loader::PublicSceLocalIdRecordResult::inserted);
    REQUIRE(libraries.record(shared, "published-library") ==
        astraea::loader::PublicSceLocalIdRecordResult::inserted);
    REQUIRE(modules.size() == 1U);
    REQUIRE(libraries.size() == 1U);
}

TEST_CASE(
    "public symbol local-ID spelling decodes canonically with strict bounds",
    "[loader][public-sce-packing][symbol-id]") {
    using astraea::loader::decode_public_sce_symbol_local_id;
    REQUIRE(decode_public_sce_symbol_local_id("A") == 0U);
    REQUIRE(decode_public_sce_symbol_local_id("B") == 1U);
    REQUIRE(decode_public_sce_symbol_local_id("-") == 63U);
    REQUIRE(decode_public_sce_symbol_local_id("BA") == 64U);
    REQUIRE(decode_public_sce_symbol_local_id("P--") == 65535U);

    for (const auto token : {"", "AA", "AB", "AAA", "QAA",
                             "BAAA", "B/", "B_", "?", " "}) {
        REQUIRE_FALSE(decode_public_sce_symbol_local_id(token).has_value());
    }
}

TEST_CASE(
    "public symbol suffix IDs must exist in independently tracked namespaces",
    "[loader][public-sce-packing][symbol-link]") {
    using astraea::loader::PublicSceLocalIdLedger;
    using astraea::loader::PublicScePackedFields;
    using astraea::loader::PublicSceLocalIdRecordResult;
    using astraea::loader::PublicSceSymbolLinkCode;
    using astraea::loader::check_public_sce_symbol_link;
    PublicSceLocalIdLedger modules;
    PublicSceLocalIdLedger libraries;
    REQUIRE(modules.record(
        PublicScePackedFields{.name_offset=40U, .version=1U, .local_id=1U},
        "published-module") == PublicSceLocalIdRecordResult::inserted);
    REQUIRE(libraries.record(
        PublicScePackedFields{.name_offset=80U, .version=1U, .local_id=0U},
        "published-library") == PublicSceLocalIdRecordResult::inserted);

    const auto valid =
        check_public_sce_symbol_link("A", "B", libraries, modules);
    REQUIRE(valid.code == PublicSceSymbolLinkCode::matched);
    REQUIRE(valid.library_local_id == 0U);
    REQUIRE(valid.module_local_id == 1U);

    const auto bad_spelling =
        check_public_sce_symbol_link("A/", "B", libraries, modules);
    REQUIRE(bad_spelling.code == PublicSceSymbolLinkCode::malformed_id);
    REQUIRE_FALSE(bad_spelling.module_local_id.has_value());
    REQUIRE_FALSE(bad_spelling.library_local_id.has_value());

    const auto missing_module =
        check_public_sce_symbol_link("A", "C", libraries, modules);
    REQUIRE(missing_module.code ==
        PublicSceSymbolLinkCode::module_unregistered);
    REQUIRE(missing_module.module_local_id == 2U);

    const auto missing_library =
        check_public_sce_symbol_link("C", "B", libraries, modules);
    REQUIRE(missing_library.code ==
        PublicSceSymbolLinkCode::library_unregistered);
    REQUIRE(missing_library.library_local_id == 2U);
}
