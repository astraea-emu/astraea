#include <astraea/probe/ps5_procparam.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

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

astraea::loader::ProgramHeader procparam_header(
    std::size_t index = 2U) {
    return astraea::loader::ProgramHeader{
        .type = 0x61000001U,
        .flags = 4U,
        .offset = 0x20U,
        .virtual_address = 0x9000U,
        .physical_address = 0U,
        .file_size = 0x60U,
        .memory_size = 0x60U,
        .alignment = 8U,
        .index = index,
    };
}

astraea::loader::ElfImage image_with(
    std::vector<astraea::loader::ProgramHeader> headers) {
    astraea::loader::ElfHeader header{};
    header.entry = 0x2400U;
    return astraea::loader::ElfImage{
        .header = header,
        .program_headers = std::move(headers),
    };
}

std::vector<std::byte> artifact_with_prefix(
    const std::array<std::byte, 16>& bytes) {
    std::vector<std::byte> artifact(0x100U);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        artifact[0x20U + i] = bytes[i];
    }
    return artifact;
}

}  // namespace

TEST_CASE(
    "PS5 procparam artifact evidence selects one static process-parameter segment",
    "[probe][c1][procparam][artifact]") {
    const auto expected_prefix = prefix(0x20U);
    const auto artifact = artifact_with_prefix(expected_prefix);
    const auto image = image_with({procparam_header()});

    const auto result =
        astraea::probe::extract_ps5_procparam_artifact_evidence(
            image,
            artifact);

    REQUIRE(result.has_value());
    REQUIRE(result->entry_image_virtual_address == 0x2400U);
    REQUIRE(result->procparam_image_virtual_address == 0x9000U);
    REQUIRE(result->procparam_prefix == expected_prefix);
}

TEST_CASE(
    "PS5 procparam artifact evidence rejects missing and duplicate segments",
    "[probe][c1][procparam][artifact][negative]") {
    const auto bytes = artifact_with_prefix(prefix(0x30U));

    SECTION("missing") {
        auto generic = procparam_header();
        generic.type = 1U;
        const auto result =
            astraea::probe::extract_ps5_procparam_artifact_evidence(
                image_with({generic}),
                bytes);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::Ps5ProcParamArtifactErrorCode::
                missing_process_parameter_segment);
    }

    SECTION("duplicate") {
        auto second = procparam_header(7U);
        const auto result =
            astraea::probe::extract_ps5_procparam_artifact_evidence(
                image_with({procparam_header(), second}),
                bytes);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::Ps5ProcParamArtifactErrorCode::
                multiple_process_parameter_segments);
        REQUIRE(result.error().program_header_index == 7U);
    }
}

TEST_CASE(
    "PS5 procparam artifact evidence rejects short and out-of-bounds prefixes",
    "[probe][c1][procparam][artifact][negative][bounds]") {
    SECTION("segment too small") {
        auto header = procparam_header();
        header.file_size = 15U;
        const auto result =
            astraea::probe::extract_ps5_procparam_artifact_evidence(
                image_with({header}),
                std::vector<std::byte>(0x100U));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::Ps5ProcParamArtifactErrorCode::
                process_parameter_segment_too_small);
    }

    SECTION("artifact bytes absent") {
        auto header = procparam_header();
        header.offset = 0xf8U;
        const auto result =
            astraea::probe::extract_ps5_procparam_artifact_evidence(
                image_with({header}),
                std::vector<std::byte>(0x100U));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::Ps5ProcParamArtifactErrorCode::
                process_parameter_prefix_out_of_bounds);
    }
}

TEST_CASE(
    "PS5 procparam observation removes ASLR through the runtime entry anchor",
    "[probe][c1][procparam]") {
    const auto expected_prefix = prefix(0x40U);
    const auto artifact =
        astraea::probe::Ps5ProcParamArtifactEvidence{
            .entry_image_virtual_address = 0x2400U,
            .procparam_image_virtual_address = 0x9000U,
            .procparam_prefix = expected_prefix,
        };

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            artifact,
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters =
                    astraea::memory::GuestAddress{
                        0x0000000922221000ULL},
                .api_return =
                    astraea::memory::GuestAddress{
                        0x0000000910009000ULL},
                .entry_runtime_address =
                    astraea::memory::GuestAddress{
                        0x0000000910002400ULL},
                .api_prefix = expected_prefix,
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
    const auto artifact =
        astraea::probe::Ps5ProcParamArtifactEvidence{
            .entry_image_virtual_address = 0x2400U,
            .procparam_image_virtual_address = 0x9000U,
            .procparam_prefix = prefix(0x50U),
        };
    const auto api_bytes = prefix(0x80U);

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            artifact,
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters =
                    astraea::memory::GuestAddress{0x30009000U},
                .api_return =
                    astraea::memory::GuestAddress{0x3000a000U},
                .entry_runtime_address =
                    astraea::memory::GuestAddress{0x30002400U},
                .api_prefix = api_bytes,
            });

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->api_matches_expected_mapped_procparam);
    REQUIRE_FALSE(result->api_prefix_matches_artifact_prefix);
    REQUIRE(result->startup_parameters_distinct_from_api_return);
}

TEST_CASE(
    "PS5 procparam observation preserves a null API return without dereference",
    "[probe][c1][procparam][null]") {
    const auto artifact =
        astraea::probe::Ps5ProcParamArtifactEvidence{
            .entry_image_virtual_address = 0x2400U,
            .procparam_image_virtual_address = 0x9000U,
            .procparam_prefix = prefix(0x60U),
        };

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            artifact,
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters =
                    astraea::memory::GuestAddress{0x40001000U},
                .api_return = astraea::memory::GuestAddress{0U},
                .entry_runtime_address =
                    astraea::memory::GuestAddress{0x40002400U},
                .api_prefix = {},
            });

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->api_return_nonzero);
    REQUIRE_FALSE(result->api_matches_expected_mapped_procparam);
    REQUIRE_FALSE(result->api_prefix_matches_artifact_prefix);
}

TEST_CASE(
    "PS5 procparam observation rejects entry load-bias underflow",
    "[probe][c1][procparam][negative][address]") {
    const auto artifact =
        astraea::probe::Ps5ProcParamArtifactEvidence{
            .entry_image_virtual_address = 0x2000U,
            .procparam_image_virtual_address = 0x3000U,
            .procparam_prefix = prefix(0x70U),
        };
    const auto api_bytes = prefix(0x70U);

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            artifact,
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters = astraea::memory::GuestAddress{1U},
                .api_return = astraea::memory::GuestAddress{2U},
                .entry_runtime_address =
                    astraea::memory::GuestAddress{0x1000U},
                .api_prefix = api_bytes,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::probe::Ps5ProcParamObservationErrorCode::
            entry_load_bias_underflow);
}

TEST_CASE(
    "PS5 procparam observation rejects mapped-address overflow",
    "[probe][c1][procparam][negative][overflow]") {
    const auto artifact =
        astraea::probe::Ps5ProcParamArtifactEvidence{
            .entry_image_virtual_address = 0U,
            .procparam_image_virtual_address = 0x100U,
            .procparam_prefix = prefix(0x80U),
        };
    const auto api_bytes = prefix(0x80U);

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            artifact,
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters = astraea::memory::GuestAddress{1U},
                .api_return = astraea::memory::GuestAddress{2U},
                .entry_runtime_address =
                    astraea::memory::GuestAddress{
                        std::numeric_limits<std::uint64_t>::max() - 0x10U},
                .api_prefix = api_bytes,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::probe::Ps5ProcParamObservationErrorCode::
            procparam_address_overflow);
}

TEST_CASE(
    "PS5 procparam observation requires bytes for a non-null API result",
    "[probe][c1][procparam][negative][size]") {
    const auto artifact =
        astraea::probe::Ps5ProcParamArtifactEvidence{
            .entry_image_virtual_address = 0x1000U,
            .procparam_image_virtual_address = 0x2000U,
            .procparam_prefix = prefix(0x90U),
        };
    const std::array<std::byte, 15> short_bytes{};

    const auto result =
        astraea::probe::validate_ps5_procparam_observation(
            artifact,
            astraea::probe::Ps5ProcParamObservation{
                .startup_parameters = astraea::memory::GuestAddress{1U},
                .api_return = astraea::memory::GuestAddress{2U},
                .entry_runtime_address =
                    astraea::memory::GuestAddress{0x3000U},
                .api_prefix = short_bytes,
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::probe::Ps5ProcParamObservationErrorCode::
            api_prefix_too_small);
}
