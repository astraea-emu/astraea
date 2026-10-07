#include <astraea/probe/ps5_procparam.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace astraea::probe {

Ps5ProcParamObservationResult
validate_ps5_procparam_observation(
    const Ps5ProcParamObservation& observation) noexcept {
    if (observation.runtime_anchor.value() <
        observation.anchor_image_virtual_address) {
        return Ps5ProcParamObservationResult::failure(
            Ps5ProcParamObservationError{
                .code =
                    Ps5ProcParamObservationErrorCode::
                        anchor_load_bias_underflow,
            });
    }

    if (observation.api_prefix.size() <
        kPs5ProcParamObservationPrefixSize) {
        return Ps5ProcParamObservationResult::failure(
            Ps5ProcParamObservationError{
                .code =
                    Ps5ProcParamObservationErrorCode::
                        api_prefix_too_small,
                .expected_size =
                    kPs5ProcParamObservationPrefixSize,
                .actual_size = observation.api_prefix.size(),
            });
    }

    if (observation.artifact_prefix.size() <
        kPs5ProcParamObservationPrefixSize) {
        return Ps5ProcParamObservationResult::failure(
            Ps5ProcParamObservationError{
                .code =
                    Ps5ProcParamObservationErrorCode::
                        artifact_prefix_too_small,
                .expected_size =
                    kPs5ProcParamObservationPrefixSize,
                .actual_size = observation.artifact_prefix.size(),
            });
    }

    const auto load_bias =
        observation.runtime_anchor.value() -
        observation.anchor_image_virtual_address;

    if (observation.procparam_image_virtual_address >
        std::numeric_limits<std::uint64_t>::max() - load_bias) {
        return Ps5ProcParamObservationResult::failure(
            Ps5ProcParamObservationError{
                .code =
                    Ps5ProcParamObservationErrorCode::
                        procparam_address_overflow,
            });
    }

    const auto expected =
        astraea::memory::GuestAddress{
            load_bias +
            observation.procparam_image_virtual_address};

    const bool prefix_matches =
        std::equal(
            observation.api_prefix.begin(),
            observation.api_prefix.begin() +
                static_cast<std::ptrdiff_t>(
                    kPs5ProcParamObservationPrefixSize),
            observation.artifact_prefix.begin());

    return Ps5ProcParamObservationResult::success(
        Ps5ProcParamProjection{
            .load_bias = load_bias,
            .expected_mapped_procparam = expected,
            .api_return_nonzero =
                observation.api_return.value() != 0U,
            .api_matches_expected_mapped_procparam =
                observation.api_return == expected,
            .api_prefix_matches_artifact_prefix =
                prefix_matches,
            .startup_parameters_distinct_from_api_return =
                observation.startup_parameters !=
                observation.api_return,
        });
}

}  // namespace astraea::probe
