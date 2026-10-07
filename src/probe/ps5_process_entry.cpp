#include <astraea/probe/ps5_process_entry.hpp>

namespace astraea::probe {

Ps5ProcessEntryObservationValidationResult
validate_ps5_process_entry_observation(
    const Ps5ProcessEntryObservation& observation) noexcept {
    if (observation.process_window_base.value() !=
        observation.rdi) {
        return Ps5ProcessEntryObservationValidationResult::failure(
            Ps5ProcessEntryObservationError{
                .code =
                    Ps5ProcessEntryObservationErrorCode::
                        process_window_base_mismatch,
                .prefix_error = std::nullopt,
            });
    }

    const auto prefix =
        astraea::execution::
            validate_ps5_direct_title_entry_prefix(
                astraea::execution::
                    Ps5DirectTitleEntryPrefixObservation{
                        .process_parameters =
                            observation.process_window_base,
                        .process_prefix =
                            observation.process_window,
                    });
    if (!prefix.has_value()) {
        return Ps5ProcessEntryObservationValidationResult::failure(
            Ps5ProcessEntryObservationError{
                .code =
                    Ps5ProcessEntryObservationErrorCode::
                        invalid_direct_title_prefix,
                .prefix_error =
                    prefix.error(),
            });
    }

    return Ps5ProcessEntryObservationValidationResult::success(
        Ps5ProcessEntryValidatedObservation{
            .direct_title_prefix =
                prefix.value(),
            .rsi =
                observation.rsi,
            .rbp =
                observation.rbp,
            .rsp =
                observation.rsp,
            .fs_base =
                observation.fs_base,
            .gs_base =
                observation.gs_base,
            .projection =
                Ps5ProcessEntryStructuralProjection{
                    .argc =
                        prefix->argc,
                    .argv0_nonzero =
                        prefix->first_argv_pointer.value() != 0U,
                    .rsi_nonzero =
                        observation.rsi != 0U,
                    .rbp_zero =
                        observation.rbp == 0U,
                    .rsp_mod16 =
                        static_cast<std::uint8_t>(
                            observation.rsp & 0x0fU),
                    .fs_base_nonzero =
                        observation.fs_base.has_value()
                            ? std::optional<bool>{
                                  observation.fs_base.value() != 0U}
                            : std::nullopt,
                    .gs_base_nonzero =
                        observation.gs_base.has_value()
                            ? std::optional<bool>{
                                  observation.gs_base.value() != 0U}
                            : std::nullopt,
                },
        });
}

Ps5ProcessEntryRepeatComparison
compare_ps5_process_entry_observations(
    const Ps5ProcessEntryValidatedObservation& first,
    const Ps5ProcessEntryValidatedObservation& second) noexcept {
    const auto& lhs = first.projection;
    const auto& rhs = second.projection;

    const auto boolean_value =
        [](bool value) noexcept
            -> std::optional<std::uint64_t> {
            return value ? 1U : 0U;
        };
    const auto optional_boolean_value =
        [](std::optional<bool> value) noexcept
            -> std::optional<std::uint64_t> {
            if (!value.has_value()) {
                return std::nullopt;
            }
            return value.value() ? 1U : 0U;
        };
    const auto difference =
        [](
            Ps5ProcessEntryProjectionField field,
            std::optional<std::uint64_t> first_value,
            std::optional<std::uint64_t> second_value)
            -> Ps5ProcessEntryRepeatComparison {
            return Ps5ProcessEntryRepeatComparison{
                .equivalent = false,
                .first_difference =
                    Ps5ProcessEntryProjectionDifference{
                        .field = field,
                        .first_value = first_value,
                        .second_value = second_value,
                    },
            };
        };

    if (lhs.argc != rhs.argc) {
        return difference(
            Ps5ProcessEntryProjectionField::argc,
            lhs.argc,
            rhs.argc);
    }
    if (lhs.argv0_nonzero != rhs.argv0_nonzero) {
        return difference(
            Ps5ProcessEntryProjectionField::
                argv0_nonzero,
            boolean_value(lhs.argv0_nonzero),
            boolean_value(rhs.argv0_nonzero));
    }
    if (lhs.rsi_nonzero != rhs.rsi_nonzero) {
        return difference(
            Ps5ProcessEntryProjectionField::
                rsi_nonzero,
            boolean_value(lhs.rsi_nonzero),
            boolean_value(rhs.rsi_nonzero));
    }
    if (lhs.rbp_zero != rhs.rbp_zero) {
        return difference(
            Ps5ProcessEntryProjectionField::
                rbp_zero,
            boolean_value(lhs.rbp_zero),
            boolean_value(rhs.rbp_zero));
    }
    if (lhs.rsp_mod16 != rhs.rsp_mod16) {
        return difference(
            Ps5ProcessEntryProjectionField::
                rsp_mod16,
            lhs.rsp_mod16,
            rhs.rsp_mod16);
    }
    if (lhs.fs_base_nonzero != rhs.fs_base_nonzero) {
        return difference(
            Ps5ProcessEntryProjectionField::
                fs_base_nonzero,
            optional_boolean_value(
                lhs.fs_base_nonzero),
            optional_boolean_value(
                rhs.fs_base_nonzero));
    }
    if (lhs.gs_base_nonzero != rhs.gs_base_nonzero) {
        return difference(
            Ps5ProcessEntryProjectionField::
                gs_base_nonzero,
            optional_boolean_value(
                lhs.gs_base_nonzero),
            optional_boolean_value(
                rhs.gs_base_nonzero));
    }

    return Ps5ProcessEntryRepeatComparison{};
}

}  // namespace astraea::probe
