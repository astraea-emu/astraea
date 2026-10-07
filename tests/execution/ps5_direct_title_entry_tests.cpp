#include <astraea/execution/ps5_direct_title_entry.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <catch2/catch_test_macros.hpp>

namespace {

void write_u32_le(
    std::span<std::byte> bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> (index * 8U)) & 0xffU));
    }
}

void write_u64_le(
    std::span<std::byte> bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> (index * 8U)) & 0xffU));
    }
}

}  // namespace

TEST_CASE(
    "PS5 direct-title C1A prefix decodes only corroborated fields",
    "[execution][c1][process-entry]") {
    std::array<
        std::byte,
        astraea::execution::
            kPs5DirectTitleEntryPrefixSize>
        bytes{};

    write_u32_le(bytes, 0U, 0x01020304U);
    write_u32_le(bytes, 4U, 0xaabbccddU);
    write_u64_le(
        bytes,
        8U,
        0x0000000812345678ULL);

    const auto result =
        astraea::execution::
            validate_ps5_direct_title_entry_prefix(
                astraea::execution::
                    Ps5DirectTitleEntryPrefixObservation{
                        .process_parameters =
                            astraea::memory::GuestAddress{
                                0x0000000800001000ULL},
                        .process_prefix = bytes,
                    });

    REQUIRE(result.has_value());
    REQUIRE(
        result->process_parameters.value() ==
        0x0000000800001000ULL);
    REQUIRE(result->argc == 0x01020304U);
    REQUIRE(
        result->argv.value() ==
        0x0000000800001008ULL);
    REQUIRE(
        result->first_argv_pointer.value() ==
        0x0000000812345678ULL);
}

TEST_CASE(
    "PS5 direct-title C1A prefix preserves opaque zero argv observation",
    "[execution][c1][process-entry][opaque]") {
    std::array<
        std::byte,
        astraea::execution::
            kPs5DirectTitleEntryPrefixSize>
        bytes{};
    write_u32_le(bytes, 0U, 0U);
    write_u64_le(bytes, 8U, 0U);

    const auto result =
        astraea::execution::
            validate_ps5_direct_title_entry_prefix(
                astraea::execution::
                    Ps5DirectTitleEntryPrefixObservation{
                        .process_parameters =
                            astraea::memory::GuestAddress{
                                0x400000U},
                        .process_prefix = bytes,
                    });

    REQUIRE(result.has_value());
    REQUIRE(result->argc == 0U);
    REQUIRE(
        result->first_argv_pointer.value() == 0U);
}

TEST_CASE(
    "PS5 direct-title C1A prefix rejects zero process-parameter address",
    "[execution][c1][process-entry][negative]") {
    std::array<
        std::byte,
        astraea::execution::
            kPs5DirectTitleEntryPrefixSize>
        bytes{};

    const auto result =
        astraea::execution::
            validate_ps5_direct_title_entry_prefix(
                astraea::execution::
                    Ps5DirectTitleEntryPrefixObservation{
                        .process_parameters =
                            astraea::memory::GuestAddress{0U},
                        .process_prefix = bytes,
                    });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            Ps5DirectTitleEntryPrefixErrorCode::
                zero_process_parameter_address);
}

TEST_CASE(
    "PS5 direct-title C1A prefix rejects truncated observation",
    "[execution][c1][process-entry][negative][size]") {
    std::array<std::byte, 15> bytes{};

    const auto result =
        astraea::execution::
            validate_ps5_direct_title_entry_prefix(
                astraea::execution::
                    Ps5DirectTitleEntryPrefixObservation{
                        .process_parameters =
                            astraea::memory::GuestAddress{
                                0x400000U},
                        .process_prefix = bytes,
                    });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            Ps5DirectTitleEntryPrefixErrorCode::
                prefix_too_small);
    REQUIRE(
        result.error().expected_size ==
        astraea::execution::
            kPs5DirectTitleEntryPrefixSize);
    REQUIRE(result.error().actual_size == 15U);
}

TEST_CASE(
    "PS5 direct-title C1A prefix rejects argv guest-address overflow",
    "[execution][c1][process-entry][negative][overflow]") {
    std::array<
        std::byte,
        astraea::execution::
            kPs5DirectTitleEntryPrefixSize>
        bytes{};

    const auto result =
        astraea::execution::
            validate_ps5_direct_title_entry_prefix(
                astraea::execution::
                    Ps5DirectTitleEntryPrefixObservation{
                        .process_parameters =
                            astraea::memory::GuestAddress{
                                std::numeric_limits<
                                    std::uint64_t>::max() -
                                3U},
                        .process_prefix = bytes,
                    });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            Ps5DirectTitleEntryPrefixErrorCode::
                argv_address_overflow);
}

TEST_CASE(
    "partial C1A evidence remains non-executable until every load-bearing contract is established",
    "[execution][c1][process-entry][readiness]") {
    const auto partial =
        astraea::execution::
            assess_ps5_direct_title_entry_readiness({});

    REQUIRE_FALSE(partial.ready());
    REQUIRE(partial.blocker_count == 5U);
    REQUIRE(
        partial.blockers[0] ==
        astraea::execution::
            Ps5DirectTitleEntryBlocker::
                loader_teardown_contract);
    REQUIRE(
        partial.blockers[1] ==
        astraea::execution::
            Ps5DirectTitleEntryBlocker::
                initial_rsp_contract);
    REQUIRE(
        partial.blockers[2] ==
        astraea::execution::
            Ps5DirectTitleEntryBlocker::
                process_metadata_contract);
    REQUIRE(
        partial.blockers[3] ==
        astraea::execution::
            Ps5DirectTitleEntryBlocker::
                primary_thread_tls_contract);
    REQUIRE(
        partial.blockers[4] ==
        astraea::execution::
            Ps5DirectTitleEntryBlocker::
                bootstrap_contract);

    const auto ready =
        astraea::execution::
            assess_ps5_direct_title_entry_readiness(
                astraea::execution::
                    Ps5DirectTitleEntryReadinessRequest{
                        .loader_teardown_contract_established =
                            true,
                        .initial_rsp_contract_established =
                            true,
                        .process_metadata_contract_established =
                            true,
                        .primary_thread_tls_contract_established =
                            true,
                        .bootstrap_contract_established =
                            true,
                    });

    REQUIRE(ready.ready());
    REQUIRE(ready.blocker_count == 0U);
}
