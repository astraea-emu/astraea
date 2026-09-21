#include <astraea/probe/reference.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

std::vector<std::byte> bytes_from_text(
    const char* text) {
    std::vector<std::byte> bytes;
    for (const char* cursor = text;
         *cursor != '\0';
         ++cursor) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    *cursor)));
    }
    return bytes;
}

}  // namespace

TEST_CASE(
    "AstraeaProbe reference echo is deterministic",
    "[probe][reference]") {
    const astraea::probe::ReferenceEchoRequest request{
        .message =
            bytes_from_text(
                "Hello from probe"),
        .exit_code = 42,
    };

    auto first =
        astraea::probe::run_reference_echo(
            request);
    auto second =
        astraea::probe::run_reference_echo(
            request);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value() == second.value());

    REQUIRE(
        first->output ==
        request.message);
    REQUIRE(first->bytes_consumed == 16);
    REQUIRE(first->exit_code == 42);
}
