#include <astraea/probe/ps5_procparam.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

std::array<std::byte, 16> prefix(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] =
            static_cast<std::byte>(
                static_cast<std::uint8_t>(seed + i));
    }
    return bytes;
}

}  // namespace

TEST_CASE(
    "PS5 procparam observation removes ASLR through an independent runtime anchor",
    "[probe][c1][procparam]") {
    const auto api_bytes = prefix(0x20U);
    const auto artifact_bytes = api_bytes;

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters =
                    astraea::memory::GuestAddress{
                        0x0000000922221000ULL},
                .api_return =
                    astraea::memory::GuestAddress{
                        0x0000000910009000ULL},
                .runtime_anchor =
                    astraea::memory::GuestAddress{
                        0x0000000910002400ULL},
                .anchor_image_virtual_address = 0x2400U,
                .procparam_image_virtual_address = 0x9000U,
                .api_prefix = api_bytes,
                .artifact_prefix = artifact_bytes,
            });

    REQUIRE(result.has_value());
    REQUIRE(result->load_bias == 0x0000000910000000ULL);
    REQUIRE(
        result->expected_mapped_procparam.value() ==
        0x0000000910009000ULL);
    REQUIRE(result->api_return_nonzero);
    REQUIRE(result->api_matches_expected_mapped_procparam);
    REQUIRE(result->api_prefix_matches_artifact_prefix);
    REQUIRE(result->startup_parameters_distinct_from_api_return);
}

TEST_CASE(
    "PS5 procparam observation keeps pointer and byte mismatches visible",
    "[probe][c1][procparam][negative]") {
    const auto api_bytes = prefix(0x10U);
    const auto artifact_bytes = prefix(0x40U);

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters =
                    astraea::memory::GuestAddress{0x30009000U},
                .api_return =
                    astraea::memory::GuestAddress{0x3000a000U},
                .runtime_anchor =
                    astraea::memory::GuestAddress{0x30002400U},
                .anchor_image_virtual_address = 0x2400U,
                .procparam_image_virtual_address = 0x9000U,
                .api_prefix = api_bytes,
                .artifact_prefix = artifact_bytes,
            });

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->api_matches_expected_mapped_procparam);
    REQUIRE_FALSE(result->api_prefix_matches_artifact_prefix);
    REQUIRE(result->startup_parameters_distinct_from_api_return);
}

TEST_CASE(
    "PS5 procparam observation preserves a null API return as evidence",
    "[probe][c1][procparam][null]") {
    const auto bytes = prefix(0x30U);

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters =
                    astraea::memory::GuestAddress{0x40001000U},
                .api_return =
                    astraea::memory::GuestAddress{0U},
                .runtime_anchor =
                    astraea::memory::GuestAddress{0x40002400U},
                .anchor_image_virtual_address = 0x2400U,
                .procparam_image_virtual_address = 0x9000U,
                .api_prefix = bytes,
                .artifact_prefix = bytes,
            });

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->api_return_nonzero);
    REQUIRE_FALSE(result->api_matches_expected_mapped_procparam);
    REQUIRE(result->api_prefix_matches_artifact_prefix);
}

TEST_CASE(
    "PS5 procparam observation rejects anchor load-bias underflow",
    "[probe][c1][procparam][negative][address]") {
    const auto bytes = prefix(0x50U);

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters = astraea::memory::GuestAddress{1U},
                .api_return = astraea::memory::GuestAddress{2U},
                .runtime_anchor = astraea::memory::GuestAddress{0x1000U},
                .anchor_image_virtual_address = 0x2000U,
                .procparam_image_virtual_address = 0x3000U,
                .api_prefix = bytes,
                .artifact_prefix = bytes,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::probe::Ps5ProcParamObservationErrorCode::
            anchor_load_bias_underflow);
}

TEST_CASE(
    "PS5 procparam observation rejects mapped-address overflow",
    "[probe][c1][procparam][negative][overflow]") {
    const auto bytes = prefix(0x60U);

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters = astraea::memory::GuestAddress{1U},
                .api_return = astraea::memory::GuestAddress{2U},
                .runtime_anchor =
                    astraea::memory::GuestAddress{
                        std::numeric_limits<std::uint64_t>::max() - 0x10U},
                .anchor_image_virtual_address = 0U,
                .procparam_image_virtual_address = 0x100U,
                .api_prefix = bytes,
                .artifact_prefix = bytes,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::probe::Ps5ProcParamObservationErrorCode::
            procparam_address_overflow);
}

TEST_CASE(
    "PS5 procparam observation rejects truncated prefixes independently",
    "[probe][c1][procparam][negative][size]") {
    const auto full = prefix(0x70U);
    const std::array<std::byte, 15> short_bytes{};

    SECTION("API prefix") {
        const auto result =
            astraea::probe::validate_ps5_procparam_observation(
                astraea::probe::Ps5ProcParamObservation{
                    .startup_parameters = astraea::memory::GuestAddress{1U},
                    .api_return = astraea::memory::GuestAddress{2U},
                    .runtime_anchor = astraea::memory::GuestAddress{0x3000U},
                    .anchor_image_virtual_address = 0x1000U,
                    .procparam_image_virtual_address = 0x2000U,
                    .api_prefix = short_bytes,
                    .artifact_prefix = full,
                });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::Ps5ProcParamObservationErrorCode::
                api_prefix_too_small);
    }

    SECTION("artifact prefix") {
        const auto result =
            astraea::probe::validate_ps5_procparam_observation(
                astraea::probe::Ps5ProcParamObservation{
                    .startup_parameters = astraea::memory::GuestAddress{1U},
                    .api_return = astraea::memory::GuestAddress{2U},
                    .runtime_anchor = astraea::memory::GuestAddress{0x3000U},
                    .anchor_image_virtual_address = 0x1000U,
                    .procparam_image_virtual_address = 0x2000U,
                    .api_prefix = full,
                    .artifact_prefix = short_bytes,
                });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::Ps5ProcParamObservationErrorCode::
                artifact_prefix_too_small);
    }
}
