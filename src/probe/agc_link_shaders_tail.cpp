#include <astraea/probe/agc_link_shaders_tail.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace astraea::probe {
namespace {

using Record =
    astraea::execution::SceAgcLinkShadersRegisterRecord;

[[nodiscard]] Record decode_record(
    std::span<const std::byte, 8> bytes) noexcept {
    std::uint32_t offset = 0;
    std::uint32_t value = 0;

    for (std::size_t index = 0; index < 4U; ++index) {
        offset |=
            static_cast<std::uint32_t>(
                std::to_integer<unsigned char>(
                    bytes[index]))
            << (index * 8U);
        value |=
            static_cast<std::uint32_t>(
                std::to_integer<unsigned char>(
                    bytes[4U + index]))
            << (index * 8U);
    }

    return Record{
        .offset = offset,
        .value = value,
    };
}

[[nodiscard]] bool all_sentinel(
    std::span<const std::byte, 8> bytes,
    std::byte sentinel) noexcept {
    return std::all_of(
        bytes.begin(),
        bytes.end(),
        [sentinel](std::byte value) {
            return value == sentinel;
        });
}

[[nodiscard]] AgcLinkShadersTailValidationError
size_error(
    AgcLinkShadersTailValidationErrorCode code,
    std::size_t expected,
    std::size_t actual) noexcept {
    return AgcLinkShadersTailValidationError{
        .code = code,
        .expected_size = expected,
        .actual_size = actual,
    };
}

[[nodiscard]] AgcLinkShadersTailValidationError
record_error(
    AgcLinkShadersTailValidationErrorCode code,
    std::size_t index,
    Record expected,
    Record actual) noexcept {
    return AgcLinkShadersTailValidationError{
        .code = code,
        .record_index = index,
        .expected_record = expected,
        .actual_record = actual,
    };
}

}  // namespace

AgcLinkShadersTailValidationResult
validate_agc_link_shaders_tail_observation(
    const AgcLinkShadersTailObservation& observation) noexcept {
    if (observation.link_return_code != 0) {
        return AgcLinkShadersTailValidationResult::failure(
            AgcLinkShadersTailValidationError{
                .code =
                    AgcLinkShadersTailValidationErrorCode::
                        link_return_failure,
                .link_return_code =
                    observation.link_return_code,
            });
    }

    if (observation.cx_raw.size() !=
        kAgcLinkShadersTailCxBytes) {
        return AgcLinkShadersTailValidationResult::failure(
            size_error(
                AgcLinkShadersTailValidationErrorCode::
                    cx_size_mismatch,
                kAgcLinkShadersTailCxBytes,
                observation.cx_raw.size()));
    }

    if (observation.uc_raw.size() !=
        kAgcLinkShadersTailUcBytes) {
        return AgcLinkShadersTailValidationResult::failure(
            size_error(
                AgcLinkShadersTailValidationErrorCode::
                    uc_size_mismatch,
                kAgcLinkShadersTailUcBytes,
                observation.uc_raw.size()));
    }

    for (std::size_t index = 0; index < 32U; ++index) {
        const auto offset = index * 8U;
        const auto actual =
            decode_record(
                std::span<const std::byte, 8>{
                    observation.cx_raw.data() + offset,
                    8U});
        const Record expected{
            .offset =
                0x191U +
                static_cast<std::uint32_t>(index),
            .value =
                static_cast<std::uint32_t>(index),
        };
        if (actual != expected) {
            return AgcLinkShadersTailValidationResult::failure(
                record_error(
                    AgcLinkShadersTailValidationErrorCode::
                        interpolant_record_mismatch,
                    index,
                    expected,
                    actual));
        }
    }

    const auto measured_routing =
        decode_record(
            std::span<const std::byte, 8>{
                observation.cx_raw.data() + 0x108U,
                8U});
    constexpr Record kExpectedRouting{
        .offset = 0x29bU,
        .value = 2U,
    };
    if (measured_routing != kExpectedRouting) {
        return AgcLinkShadersTailValidationResult::failure(
            record_error(
                AgcLinkShadersTailValidationErrorCode::
                    measured_routing_record_mismatch,
                33U,
                kExpectedRouting,
                measured_routing));
    }

    AgcLinkShadersTailValidatedObservation validated{};
    std::copy(
        observation.cx_raw.begin(),
        observation.cx_raw.end(),
        validated.cx_raw.begin());
    std::copy(
        observation.uc_raw.begin(),
        observation.uc_raw.end(),
        validated.uc_raw.begin());

    const std::span<const std::byte, 8>
        context_tail_bytes{
            observation.cx_raw.data() + 0x100U,
            8U};
    validated.unknown_context_record =
        decode_record(context_tail_bytes);
    validated.unknown_context_record_matches_sentinel =
        all_sentinel(
            context_tail_bytes,
            observation.cx_sentinel);

    for (std::size_t index = 0; index < 3U; ++index) {
        const std::span<const std::byte, 8> bytes{
            observation.uc_raw.data() + index * 8U,
            8U};
        validated.unknown_user_config_records[index] =
            decode_record(bytes);
        validated
            .unknown_user_config_record_matches_sentinel[index] =
            all_sentinel(
                bytes,
                observation.uc_sentinel);
    }

    return AgcLinkShadersTailValidationResult::success(
        validated);
}

AgcLinkShadersTailRepeatComparison
compare_agc_link_shaders_tail_runs(
    const AgcLinkShadersTailValidatedObservation& first,
    const AgcLinkShadersTailValidatedObservation& second) noexcept {
    for (std::size_t index = 0;
         index < first.cx_raw.size();
         ++index) {
        if (first.cx_raw[index] != second.cx_raw[index]) {
            return AgcLinkShadersTailRepeatComparison{
                .equivalent = false,
                .first_difference =
                    AgcLinkShadersTailRepeatDifference{
                        .region =
                            AgcLinkShadersTailRepeatDifferenceRegion::
                                context,
                        .byte_offset = index,
                        .first = first.cx_raw[index],
                        .second = second.cx_raw[index],
                    },
            };
        }
    }

    for (std::size_t index = 0;
         index < first.uc_raw.size();
         ++index) {
        if (first.uc_raw[index] != second.uc_raw[index]) {
            return AgcLinkShadersTailRepeatComparison{
                .equivalent = false,
                .first_difference =
                    AgcLinkShadersTailRepeatDifference{
                        .region =
                            AgcLinkShadersTailRepeatDifferenceRegion::
                                user_config,
                        .byte_offset = index,
                        .first = first.uc_raw[index],
                        .second = second.uc_raw[index],
                    },
            };
        }
    }

    return AgcLinkShadersTailRepeatComparison{};
}

}  // namespace astraea::probe
