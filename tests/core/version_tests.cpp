#include <astraea/core/version.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Astraea reports a non-empty development version", "[core][version]") {
    const auto version = astraea::core::version();

    REQUIRE_FALSE(version.empty());
    REQUIRE(version == "0.0.0-dev");
}
