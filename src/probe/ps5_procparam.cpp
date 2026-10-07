#include <astraea/probe/ps5_procparam.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include <astraea/loader/sce_program_header.hpp>

namespace astraea::probe {

Ps5ProcParamArtifactResult
extract_ps5_procparam_artifact_evidence(
    const astraea::loader::ElfImage& image,
    std::span<const std::byte> artifact_bytes) noexcept {
    const astraea::loader::ProgramHeader* selected = nullptr;

    for (const auto& header : image.program_headers) {
        if (astraea::loader::classify_sce_program_header_type(
                header.type) !=
            astraea::loader::SceProgramHeaderKind::process_parameter) {
            continue;
        }

        if (selected != nullptr) {
            return Ps5ProcParamArtifactResult::failure(
                Ps5ProcParamArtifactError{
                    .code =
                        Ps5ProcParamArtifactErrorCode::
                            multiple_process_parameter_segments,
                    .program_header_index = header.index,
                });
        }
        selected = &header;
    }

    if (selected == nullptr) {
        return Ps5ProcParamArtifactResult::failure(
            Ps5ProcParamArtifactError{
                .code =
                    Ps5ProcParamArtifactErrorCode::
                        missing_process_parameter_segment,
            });
    }

    if (selected->file_size < kPs5ProcParamObservationPrefixSize) {
        return Ps5ProcParamArtifactResult::failure(
            Ps5ProcParamArtifactError{
                .code =
                    Ps5ProcParamArtifactErrorCode::
                        process_parameter_segment_too_small,
                .program_header_index = selected->index,
            });
    }

    constexpr auto prefix_size =
        static_cast<std::uint64_t>(
            kPs5ProcParamObservationPrefixSize);
    if (selected->offset >
            std::numeric_limits<std::uint64_t>::max() - prefix_size ||
        selected->offset + prefix_size >
            static_cast<std::uint64_t>(artifact_bytes.size())) {
        return Ps5ProcParamArtifactResult::failure(
            Ps5ProcParamArtifactError{
                .code =
                    Ps5ProcParamArtifactErrorCode::
                        process_parameter_prefix_out_of_bounds,
                .program_header_index = selected->index,
            });
    }

    Ps5ProcParamArtifactEvidence result{
        .entry_image_virtual_address = image.header.entry,
        .procparam_image_virtual_address = selected->virtual_address,
    };
    std::copy_n(
        artifact_bytes.begin() +
            static_cast<std::ptrdiff_t>(selected->offset),
        kPs5ProcParamObservationPrefixSize,
        result.procparam_prefix.begin());

    return Ps5ProcParamArtifactResult::success(result);
}

Ps5ProcParamObservationResult
validate_ps5_procparam_observation(
    const Ps5ProcParamArtifactEvidence& artifact,
    const Ps5ProcParamObservation& observation) noexcept {
    if (observation.entry_runtime_address.value() <
        artifact.entry_image_virtual_address) {
        return Ps5ProcParamObservationResult::failure(
            Ps5ProcParamObservationError{
                .code =
                    Ps5ProcParamObservationErrorCode::
                        entry_load_bias_underflow,
            });
    }

    const bool api_return_nonzero =
        observation.api_return.value() != 0U;
    if (api_return_nonzero &&
        observation.api_prefix.size() <
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

    const auto load_bias =
        observation.entry_runtime_address.value() -
        artifact.entry_image_virtual_address;

    if (artifact.procparam_image_virtual_address >
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
            artifact.procparam_image_virtual_address};

    const bool prefix_matches =
        api_return_nonzero &&
        std::equal(
            observation.api_prefix.begin(),
            observation.api_prefix.begin() +
                static_cast<std::ptrdiff_t>(
                    kPs5ProcParamObservationPrefixSize),
            artifact.procparam_prefix.begin());

    return Ps5ProcParamObservationResult::success(
        Ps5ProcParamProjection{
            .load_bias = load_bias,
            .expected_mapped_procparam = expected,
            .api_return_nonzero = api_return_nonzero,
            .api_matches_expected_mapped_procparam =
                observation.api_return == expected,
            .api_prefix_matches_artifact_prefix = prefix_matches,
            .startup_parameters_distinct_from_api_return =
                observation.startup_parameters != observation.api_return,
        });
}

}  // namespace astraea::probe
