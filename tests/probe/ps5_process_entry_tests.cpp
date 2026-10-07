#include <astraea/probe/ps5_process_entry.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

struct ObservationFixture {
    std::array<
        std::byte,
        astraea::execution::
            kPs5DirectTitleEntryPrefixSize>
        process{};

    std::uint64_t rdi = 0x0000000800001000ULL;
    std::uint64_t rsi = 0x0000000800100000ULL;
    std::uint64_t rbp = 0U;
    std::uint64_t rsp = 0x000000087fff0008ULL;
    std::optional<std::uint64_t> fs_base =
        0x0000000810000000ULL;
    std::optional<std::uint64_t> gs_base = 0U;

    ObservationFixture() {
        write_u32_le(process, 0U, 1U);
        write_u64_le(
            process,
            8U,
            0x0000000800002000ULL);
    }

    [[nodiscard]] astraea::probe::
        Ps5ProcessEntryObservation
    observation() const {
        return astraea::probe::
            Ps5ProcessEntryObservation{
                .rdi = rdi,
                .rsi = rsi,
                .rbp = rbp,
                .rsp = rsp,
                .fs_base = fs_base,
                .gs_base = gs_base,
                .process_window_base =
                    astraea::memory::GuestAddress{
                        rdi},
                .process_window =
                    process,
            };
    }
};

astraea::probe::Ps5ProcessEntryValidatedObservation
validated(const ObservationFixture& fixture) {
    const auto result =
        astraea::probe::
            validate_ps5_process_entry_observation(
                fixture.observation());
    REQUIRE(result.has_value());
    return result.value();
}

void require_difference(
    const astraea::probe::
        Ps5ProcessEntryValidatedObservation& first,
    const astraea::probe::
        Ps5ProcessEntryValidatedObservation& second,
    astraea::probe::Ps5ProcessEntryProjectionField
        field) {
    const auto comparison =
        astraea::probe::
            compare_ps5_process_entry_observations(
                first,
                second);

    REQUIRE_FALSE(comparison.equivalent);
    REQUIRE(comparison.first_difference.has_value());
    REQUIRE(
        comparison.first_difference->field ==
        field);
}

}  // namespace

TEST_CASE(
    "PS5 process-entry observation validates direct-title prefix and derives structural projection",
    "[probe][c1][process-entry]") {
    const ObservationFixture fixture{};
    const auto result =
        astraea::probe::
            validate_ps5_process_entry_observation(
                fixture.observation());

    REQUIRE(result.has_value());
    REQUIRE(result->direct_title_prefix.argc == 1U);
    REQUIRE(
        result->direct_title_prefix.argv.value() ==
        fixture.rdi + 8U);
    REQUIRE(
        result->direct_title_prefix.
            first_argv_pointer.value() ==
        0x0000000800002000ULL);

    REQUIRE(result->rsi == fixture.rsi);
    REQUIRE(result->rbp == fixture.rbp);
    REQUIRE(result->rsp == fixture.rsp);
    REQUIRE(result->fs_base == fixture.fs_base);
    REQUIRE(result->gs_base == fixture.gs_base);

    REQUIRE(result->projection.argc == 1U);
    REQUIRE(result->projection.argv0_nonzero);
    REQUIRE(result->projection.rsi_nonzero);
    REQUIRE(result->projection.rbp_zero);
    REQUIRE(result->projection.rsp_mod16 == 8U);
    REQUIRE(
        result->projection.fs_base_nonzero ==
        std::optional<bool>{true});
    REQUIRE(
        result->projection.gs_base_nonzero ==
        std::optional<bool>{false});
}

TEST_CASE(
    "PS5 process-entry observation rejects RDI and captured-window mismatch",
    "[probe][c1][process-entry][negative]") {
    ObservationFixture fixture{};
    auto observation = fixture.observation();
    observation.process_window_base =
        astraea::memory::GuestAddress{
            fixture.rdi + 0x1000U};

    const auto result =
        astraea::probe::
            validate_ps5_process_entry_observation(
                observation);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::probe::
            Ps5ProcessEntryObservationErrorCode::
                process_window_base_mismatch);
    REQUIRE_FALSE(result.error().prefix_error.has_value());
}

TEST_CASE(
    "PS5 process-entry observation preserves typed prefix validation failure",
    "[probe][c1][process-entry][negative][prefix]") {
    ObservationFixture fixture{};
    auto observation = fixture.observation();
    observation.process_window =
        std::span<const std::byte>{
            fixture.process.data(),
            fixture.process.size() - 1U};

    const auto result =
        astraea::probe::
            validate_ps5_process_entry_observation(
                observation);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::probe::
            Ps5ProcessEntryObservationErrorCode::
                invalid_direct_title_prefix);
    REQUIRE(result.error().prefix_error.has_value());
    REQUIRE(
        result.error().prefix_error->code ==
        astraea::execution::
            Ps5DirectTitleEntryPrefixErrorCode::
                prefix_too_small);
}

TEST_CASE(
    "PS5 process-entry observation reports every RSP modulo-16 residue without promoting one",
    "[probe][c1][process-entry][rsp]") {
    for (std::uint64_t residue = 0U;
         residue < 16U;
         ++residue) {
        ObservationFixture fixture{};
        fixture.rsp =
            0x000000087fff0000ULL +
            residue;

        const auto result =
            astraea::probe::
                validate_ps5_process_entry_observation(
                    fixture.observation());

        REQUIRE(result.has_value());
        REQUIRE(
            result->projection.rsp_mod16 ==
            static_cast<std::uint8_t>(
                residue));
    }
}

TEST_CASE(
    "PS5 process-entry structural projection preserves zero and nonzero observations",
    "[probe][c1][process-entry][structural]") {
    ObservationFixture fixture{};
    fixture.rsi = 0U;
    fixture.rbp = 1U;
    fixture.fs_base = 0U;
    fixture.gs_base = 2U;
    write_u64_le(
        fixture.process,
        8U,
        0U);

    const auto result =
        astraea::probe::
            validate_ps5_process_entry_observation(
                fixture.observation());

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->projection.argv0_nonzero);
    REQUIRE_FALSE(result->projection.rsi_nonzero);
    REQUIRE_FALSE(result->projection.rbp_zero);
    REQUIRE(
        result->projection.fs_base_nonzero ==
        std::optional<bool>{false});
    REQUIRE(
        result->projection.gs_base_nonzero ==
        std::optional<bool>{true});
}


TEST_CASE(
    "PS5 process-entry observation distinguishes unavailable segment bases from observed zero",
    "[probe][c1][process-entry][segment-base][unknown]") {
    ObservationFixture unavailable{};
    unavailable.fs_base = std::nullopt;
    unavailable.gs_base = std::nullopt;

    const auto unknown =
        validated(unavailable);

    REQUIRE_FALSE(unknown.fs_base.has_value());
    REQUIRE_FALSE(unknown.gs_base.has_value());
    REQUIRE_FALSE(
        unknown.projection.fs_base_nonzero.has_value());
    REQUIRE_FALSE(
        unknown.projection.gs_base_nonzero.has_value());

    ObservationFixture observed_zero{};
    observed_zero.fs_base = 0U;
    observed_zero.gs_base = 0U;

    const auto zero =
        validated(observed_zero);

    REQUIRE(
        zero.projection.fs_base_nonzero ==
        std::optional<bool>{false});
    REQUIRE(
        zero.projection.gs_base_nonzero ==
        std::optional<bool>{false});

    require_difference(
        unknown,
        zero,
        astraea::probe::
            Ps5ProcessEntryProjectionField::
                fs_base_nonzero);

    const auto comparison =
        astraea::probe::
            compare_ps5_process_entry_observations(
                unknown,
                zero);
    REQUIRE(comparison.first_difference.has_value());
    REQUIRE_FALSE(
        comparison.first_difference->
            first_value.has_value());
    REQUIRE(
        comparison.first_difference->
            second_value ==
        std::optional<std::uint64_t>{0U});
}

TEST_CASE(
    "PS5 process-entry repeated observations compare structural projection only",
    "[probe][c1][process-entry][repeat]") {
    const ObservationFixture baseline_fixture{};
    const auto baseline =
        validated(baseline_fixture);

    const auto same =
        astraea::probe::
            compare_ps5_process_entry_observations(
                baseline,
                validated(baseline_fixture));
    REQUIRE(same.equivalent);
    REQUIRE_FALSE(same.first_difference.has_value());

    SECTION("argc") {
        ObservationFixture changed{};
        write_u32_le(changed.process, 0U, 2U);
        require_difference(
            baseline,
            validated(changed),
            astraea::probe::
                Ps5ProcessEntryProjectionField::argc);
    }

    SECTION("argv0 presence") {
        ObservationFixture changed{};
        write_u64_le(changed.process, 8U, 0U);
        require_difference(
            baseline,
            validated(changed),
            astraea::probe::
                Ps5ProcessEntryProjectionField::
                    argv0_nonzero);
    }

    SECTION("RSI presence") {
        ObservationFixture changed{};
        changed.rsi = 0U;
        require_difference(
            baseline,
            validated(changed),
            astraea::probe::
                Ps5ProcessEntryProjectionField::
                    rsi_nonzero);
    }

    SECTION("RBP zero state") {
        ObservationFixture changed{};
        changed.rbp = 1U;
        require_difference(
            baseline,
            validated(changed),
            astraea::probe::
                Ps5ProcessEntryProjectionField::
                    rbp_zero);
    }

    SECTION("RSP alignment residue") {
        ObservationFixture changed{};
        changed.rsp += 1U;
        const auto changed_observation =
            validated(changed);
        require_difference(
            baseline,
            changed_observation,
            astraea::probe::
                Ps5ProcessEntryProjectionField::
                    rsp_mod16);

        const auto comparison =
            astraea::probe::
                compare_ps5_process_entry_observations(
                    baseline,
                    changed_observation);
        REQUIRE(
            comparison.first_difference->
                first_value ==
            std::optional<std::uint64_t>{8U});
        REQUIRE(
            comparison.first_difference->
                second_value ==
            std::optional<std::uint64_t>{9U});
    }

    SECTION("FS presence") {
        ObservationFixture changed{};
        changed.fs_base = 0U;
        require_difference(
            baseline,
            validated(changed),
            astraea::probe::
                Ps5ProcessEntryProjectionField::
                    fs_base_nonzero);
    }

    SECTION("GS presence") {
        ObservationFixture changed{};
        changed.gs_base = 3U;
        require_difference(
            baseline,
            validated(changed),
            astraea::probe::
                Ps5ProcessEntryProjectionField::
                    gs_base_nonzero);
    }
}

TEST_CASE(
    "PS5 process-entry repeat comparison ignores raw-address changes when structural facts match",
    "[probe][c1][process-entry][repeat][normalization]") {
    ObservationFixture first_fixture{};
    ObservationFixture second_fixture{};

    second_fixture.rsi += 0x10000U;
    second_fixture.rbp = 0U;
    second_fixture.rsp += 0x1000U;
    second_fixture.fs_base =
        second_fixture.fs_base.value() + 0x20000U;
    second_fixture.gs_base = 0U;

    write_u64_le(
        second_fixture.process,
        8U,
        0x0000000900002000ULL);

    const auto comparison =
        astraea::probe::
            compare_ps5_process_entry_observations(
                validated(first_fixture),
                validated(second_fixture));

    REQUIRE(comparison.equivalent);
    REQUIRE_FALSE(comparison.first_difference.has_value());
}
