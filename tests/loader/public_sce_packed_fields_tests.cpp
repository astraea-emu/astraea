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
