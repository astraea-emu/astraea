#include <astraea/loader/sce_symbol_identity.hpp>

#include <string>

#include <catch2/catch_test_macros.hpp>

using astraea::loader::SceSymbolIdentityErrorCode;

TEST_CASE(
    "plain SCE dynamic symbol names remain opaque and exact",
    "[loader][sce-symbol-identity]") {
    SECTION("ordinary plain name") {
        const std::string raw = "plain_symbol_name";

        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                raw);

        REQUIRE(result.has_value());
        REQUIRE(result->raw == raw);
        REQUIRE_FALSE(result->identity.has_value());
    }

    SECTION("empty plain name") {
        const std::string raw;

        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                raw);

        REQUIRE(result.has_value());
        REQUIRE(result->raw.empty());
        REQUIRE_FALSE(result->identity.has_value());
    }
}

TEST_CASE(
    "valid SCE long-form symbol identity splits without interpreting components",
    "[loader][sce-symbol-identity]") {
    const std::string raw =
        "ABCDEFGHIJK#lib.01#module-02";

    const auto result =
        astraea::loader::parse_sce_dynamic_symbol_name(
            raw);

    REQUIRE(result.has_value());
    REQUIRE(result->raw == raw);
    REQUIRE(result->identity.has_value());
    REQUIRE(result->identity->nid == "ABCDEFGHIJK");
    REQUIRE(result->identity->library_id == "lib.01");
    REQUIRE(result->identity->module_id == "module-02");
}

TEST_CASE(
    "candidate SCE long form requires exactly two separators",
    "[loader][sce-symbol-identity]") {
    SECTION("one separator") {
        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                "ABCDEFGHIJK#lib");

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceSymbolIdentityErrorCode::
                malformed_long_form);
    }

    SECTION("three separators") {
        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                "ABCDEFGHIJK#lib#module#extra");

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceSymbolIdentityErrorCode::
                malformed_long_form);
    }
}

TEST_CASE(
    "candidate SCE long form rejects empty components",
    "[loader][sce-symbol-identity]") {
    SECTION("empty nid") {
        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                "#lib#module");

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceSymbolIdentityErrorCode::
                malformed_long_form);
    }

    SECTION("empty library id") {
        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                "ABCDEFGHIJK##module");

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceSymbolIdentityErrorCode::
                malformed_long_form);
    }

    SECTION("empty module id") {
        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                "ABCDEFGHIJK#lib#");

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceSymbolIdentityErrorCode::
                malformed_long_form);
    }
}

TEST_CASE(
    "candidate SCE long form requires the observed 11-character NID spelling",
    "[loader][sce-symbol-identity]") {
    SECTION("too short") {
        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                "ABCDEFGHIJ#lib#module");

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceSymbolIdentityErrorCode::
                invalid_nid_length);
    }

    SECTION("too long") {
        const auto result =
            astraea::loader::parse_sce_dynamic_symbol_name(
                "ABCDEFGHIJKL#lib#module");

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceSymbolIdentityErrorCode::
                invalid_nid_length);
    }
}
