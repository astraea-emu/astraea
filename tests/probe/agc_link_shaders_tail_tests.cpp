#include <astraea/probe/agc_link_shaders_tail.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::SceAgcLinkShadersRegisterRecord;
using astraea::probe::AgcLinkShadersTailObservation;
using astraea::probe::AgcLinkShadersTailRepeatDifferenceRegion;
using astraea::probe::AgcLinkShadersTailValidationErrorCode;

void write_record(
    std::span<std::byte, 8> bytes,
    std::uint32_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[index] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (offset >> (index * 8U)) &
                    0xffU));
        bytes[4U + index] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> (index * 8U)) &
                    0xffU));
    }
}

struct ObservationBytes {
    std::array<
        std::byte,
        astraea::probe::kAgcLinkShadersTailCxBytes>
        cx{};
    std::array<
        std::byte,
        astraea::probe::kAgcLinkShadersTailUcBytes>
        uc{};
};

ObservationBytes make_valid_bytes(
    std::byte cx_sentinel = std::byte{0xa5},
    std::byte uc_sentinel = std::byte{0x5a}) {
    ObservationBytes bytes{};
    bytes.cx.fill(cx_sentinel);
    bytes.uc.fill(uc_sentinel);

    for (std::size_t index = 0; index < 32U; ++index) {
        write_record(
            std::span<std::byte, 8>{
                bytes.cx.data() + index * 8U,
                8U},
            0x191U +
                static_cast<std::uint32_t>(index),
            static_cast<std::uint32_t>(index));
    }

    write_record(
        std::span<std::byte, 8>{
            bytes.cx.data() + 0x108U,
            8U},
        0x29bU,
        2U);

    return bytes;
}

AgcLinkShadersTailObservation observation(
    const ObservationBytes& bytes,
    std::byte cx_sentinel = std::byte{0xa5},
    std::byte uc_sentinel = std::byte{0x5a},
    std::int64_t return_code = 0) {
    return AgcLinkShadersTailObservation{
        .link_return_code = return_code,
        .cx_raw = bytes.cx,
        .uc_raw = bytes.uc,
        .cx_sentinel = cx_sentinel,
        .uc_sentinel = uc_sentinel,
    };
}

}  // namespace

TEST_CASE(
    "LinkShaders tail observation validates known records and extracts opaque tail",
    "[probe][agc][link-shaders-tail]") {
    auto bytes = make_valid_bytes();

    write_record(
        std::span<std::byte, 8>{
            bytes.cx.data() + 0x100U,
            8U},
        0x123U,
        0x456789abU);
    write_record(
        std::span<std::byte, 8>{
            bytes.uc.data(),
            8U},
        0x234U,
        0x10203040U);
    write_record(
        std::span<std::byte, 8>{
            bytes.uc.data() + 8U,
            8U},
        0x345U,
        0x50607080U);
    write_record(
        std::span<std::byte, 8>{
            bytes.uc.data() + 16U,
            8U},
        0x456U,
        0x90abcdefU);

    const auto result =
        astraea::probe::
            validate_agc_link_shaders_tail_observation(
                observation(bytes));

    REQUIRE(result.has_value());
    REQUIRE(
        result->unknown_context_record ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x123U,
            .value = 0x456789abU,
        });
    REQUIRE_FALSE(
        result->unknown_context_record_untouched);

    REQUIRE(
        result->unknown_user_config_records[0] ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x234U,
            .value = 0x10203040U,
        });
    REQUIRE(
        result->unknown_user_config_records[1] ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x345U,
            .value = 0x50607080U,
        });
    REQUIRE(
        result->unknown_user_config_records[2] ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x456U,
            .value = 0x90abcdefU,
        });
    REQUIRE_FALSE(
        result->unknown_user_config_record_untouched[0]);
    REQUIRE_FALSE(
        result->unknown_user_config_record_untouched[1]);
    REQUIRE_FALSE(
        result->unknown_user_config_record_untouched[2]);

    REQUIRE(result->cx_raw == bytes.cx);
    REQUIRE(result->uc_raw == bytes.uc);
}

TEST_CASE(
    "LinkShaders tail observation tracks untouched sentinel records independently",
    "[probe][agc][link-shaders-tail][sentinel]") {
    auto bytes = make_valid_bytes();

    // CX[32] and UC[0] remain the sentinel patterns.
    write_record(
        std::span<std::byte, 8>{
            bytes.uc.data() + 8U,
            8U},
        0x777U,
        0x11111111U);
    write_record(
        std::span<std::byte, 8>{
            bytes.uc.data() + 16U,
            8U},
        0x888U,
        0x22222222U);

    const auto result =
        astraea::probe::
            validate_agc_link_shaders_tail_observation(
                observation(bytes));

    REQUIRE(result.has_value());
    REQUIRE(
        result->unknown_context_record_untouched);
    REQUIRE(
        result->unknown_user_config_record_untouched[0]);
    REQUIRE_FALSE(
        result->unknown_user_config_record_untouched[1]);
    REQUIRE_FALSE(
        result->unknown_user_config_record_untouched[2]);
}

TEST_CASE(
    "LinkShaders tail observation rejects nonzero native return code",
    "[probe][agc][link-shaders-tail][negative]") {
    const auto bytes = make_valid_bytes();

    const auto result =
        astraea::probe::
            validate_agc_link_shaders_tail_observation(
                observation(
                    bytes,
                    std::byte{0xa5},
                    std::byte{0x5a},
                    -7));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        AgcLinkShadersTailValidationErrorCode::
            link_return_failure);
    REQUIRE(result.error().link_return_code == -7);
}

TEST_CASE(
    "LinkShaders tail observation rejects wrong raw block sizes",
    "[probe][agc][link-shaders-tail][negative][size]") {
    const auto bytes = make_valid_bytes();

    SECTION("context") {
        const AgcLinkShadersTailObservation request{
            .link_return_code = 0,
            .cx_raw =
                std::span<const std::byte>{
                    bytes.cx.data(),
                    bytes.cx.size() - 1U},
            .uc_raw = bytes.uc,
            .cx_sentinel = std::byte{0xa5},
            .uc_sentinel = std::byte{0x5a},
        };

        const auto result =
            astraea::probe::
                validate_agc_link_shaders_tail_observation(
                    request);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            AgcLinkShadersTailValidationErrorCode::
                cx_size_mismatch);
        REQUIRE(
            result.error().expected_size ==
            astraea::probe::
                kAgcLinkShadersTailCxBytes);
        REQUIRE(
            result.error().actual_size ==
            bytes.cx.size() - 1U);
    }

    SECTION("user config") {
        const AgcLinkShadersTailObservation request{
            .link_return_code = 0,
            .cx_raw = bytes.cx,
            .uc_raw =
                std::span<const std::byte>{
                    bytes.uc.data(),
                    bytes.uc.size() - 1U},
            .cx_sentinel = std::byte{0xa5},
            .uc_sentinel = std::byte{0x5a},
        };

        const auto result =
            astraea::probe::
                validate_agc_link_shaders_tail_observation(
                    request);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            AgcLinkShadersTailValidationErrorCode::
                uc_size_mismatch);
        REQUIRE(
            result.error().expected_size ==
            astraea::probe::
                kAgcLinkShadersTailUcBytes);
        REQUIRE(
            result.error().actual_size ==
            bytes.uc.size() - 1U);
    }
}

TEST_CASE(
    "LinkShaders tail observation identifies first bad interpolant record",
    "[probe][agc][link-shaders-tail][negative][known]") {
    auto bytes = make_valid_bytes();

    write_record(
        std::span<std::byte, 8>{
            bytes.cx.data() + 7U * 8U,
            8U},
        0x198U,
        0xdeadbeefU);

    const auto result =
        astraea::probe::
            validate_agc_link_shaders_tail_observation(
                observation(bytes));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        AgcLinkShadersTailValidationErrorCode::
            interpolant_record_mismatch);
    REQUIRE(
        result.error().record_index ==
        std::optional<std::size_t>{7U});
    REQUIRE(
        result.error().expected_record ==
        std::optional<SceAgcLinkShadersRegisterRecord>{
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x198U,
                .value = 7U,
            }});
    REQUIRE(
        result.error().actual_record ==
        std::optional<SceAgcLinkShadersRegisterRecord>{
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x198U,
                .value = 0xdeadbeefU,
            }});
}

TEST_CASE(
    "LinkShaders tail observation rejects measured routing divergence",
    "[probe][agc][link-shaders-tail][negative][known]") {
    auto bytes = make_valid_bytes();

    write_record(
        std::span<std::byte, 8>{
            bytes.cx.data() + 0x108U,
            8U},
        0x29bU,
        3U);

    const auto result =
        astraea::probe::
            validate_agc_link_shaders_tail_observation(
                observation(bytes));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        AgcLinkShadersTailValidationErrorCode::
            measured_routing_record_mismatch);
    REQUIRE(
        result.error().record_index ==
        std::optional<std::size_t>{33U});
    REQUIRE(
        result.error().expected_record ==
        std::optional<SceAgcLinkShadersRegisterRecord>{
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x29bU,
                .value = 2U,
            }});
    REQUIRE(
        result.error().actual_record ==
        std::optional<SceAgcLinkShadersRegisterRecord>{
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x29bU,
                .value = 3U,
            }});
}

TEST_CASE(
    "LinkShaders tail repeated observations compare complete raw output",
    "[probe][agc][link-shaders-tail][repeat]") {
    auto first_bytes = make_valid_bytes();
    write_record(
        std::span<std::byte, 8>{
            first_bytes.cx.data() + 0x100U,
            8U},
        0x111U,
        0x22222222U);
    write_record(
        std::span<std::byte, 8>{
            first_bytes.uc.data(),
            8U},
        0x333U,
        0x44444444U);

    const auto first =
        astraea::probe::
            validate_agc_link_shaders_tail_observation(
                observation(first_bytes));
    const auto second =
        astraea::probe::
            validate_agc_link_shaders_tail_observation(
                observation(first_bytes));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    const auto same =
        astraea::probe::
            compare_agc_link_shaders_tail_runs(
                first.value(),
                second.value());
    REQUIRE(same.equivalent);
    REQUIRE_FALSE(same.first_difference.has_value());

    SECTION("context divergence") {
        auto changed = first.value();
        changed.cx_raw[0x103U] = std::byte{0xee};

        const auto comparison =
            astraea::probe::
                compare_agc_link_shaders_tail_runs(
                    first.value(),
                    changed);

        REQUIRE_FALSE(comparison.equivalent);
        REQUIRE(comparison.first_difference.has_value());
        REQUIRE(
            comparison.first_difference->region ==
            AgcLinkShadersTailRepeatDifferenceRegion::
                context);
        REQUIRE(
            comparison.first_difference->byte_offset ==
            0x103U);
        REQUIRE(
            comparison.first_difference->first ==
            first->cx_raw[0x103U]);
        REQUIRE(
            comparison.first_difference->second ==
            std::byte{0xee});
    }

    SECTION("user-config divergence") {
        auto changed = first.value();
        changed.uc_raw[9U] = std::byte{0xdd};

        const auto comparison =
            astraea::probe::
                compare_agc_link_shaders_tail_runs(
                    first.value(),
                    changed);

        REQUIRE_FALSE(comparison.equivalent);
        REQUIRE(comparison.first_difference.has_value());
        REQUIRE(
            comparison.first_difference->region ==
            AgcLinkShadersTailRepeatDifferenceRegion::
                user_config);
        REQUIRE(
            comparison.first_difference->byte_offset ==
            9U);
        REQUIRE(
            comparison.first_difference->first ==
            first->uc_raw[9U]);
        REQUIRE(
            comparison.first_difference->second ==
            std::byte{0xdd});
    }
}
