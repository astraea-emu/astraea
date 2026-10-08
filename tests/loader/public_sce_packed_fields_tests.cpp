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
