#include <astraea/core/result.hpp>

#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Result stores and exposes success values", "[core][result]") {
    auto result = astraea::core::Result<std::string, int>::success("astraea");

    REQUIRE(result.has_value());
    REQUIRE(static_cast<bool>(result));
    REQUIRE(result.value() == "astraea");
    REQUIRE(result->size() == 7);
    REQUIRE((*result) == "astraea");
}

TEST_CASE("Result stores and exposes errors", "[core][result]") {
    auto result = astraea::core::Result<std::string, int>::failure(42);

    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(static_cast<bool>(result));
    REQUIRE(result.error() == 42);
}
