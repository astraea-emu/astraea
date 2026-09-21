#include <astraea/trace/diff_v0.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace astraea::trace {
namespace {

[[nodiscard]] TraceDiffError diff_error(
    TraceDiffErrorCode code,
    std::optional<TraceError> trace_error =
        std::nullopt) noexcept {
    return TraceDiffError{
        .code = code,
        .has_trace_error = trace_error.has_value(),
        .trace_error =
            trace_error.has_value()
                ? trace_error.value()
                : TraceError{},
    };
}

[[nodiscard]] bool identifier_char(
    char value) noexcept {
    return (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') ||
           value == '.' ||
           value == '_' ||
           value == ':' ||
           value == '-';
}

[[nodiscard]] bool valid_identifier(
    std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }

    const char first = value.front();
    if (!((first >= 'a' && first <= 'z') ||
          (first >= '0' && first <= '9'))) {
        return false;
    }

    return std::all_of(
        value.begin(),
        value.end(),
        [](char character) {
            return identifier_char(character);
        });
}

[[nodiscard]] bool valid_ignore_rule(
    const TraceStableFieldIgnoreRuleV0& rule)
    noexcept {
    if (!valid_identifier(rule.field)) {
        return false;
    }

    if (rule.subsystem.has_value() &&
        !valid_identifier(
            rule.subsystem.value())) {
        return false;
    }

    if (rule.type.has_value() &&
        !valid_identifier(
            rule.type.value())) {
        return false;
    }

    return true;
}

[[nodiscard]] bool ignore_rule_less(
    const TraceStableFieldIgnoreRuleV0& lhs,
    const TraceStableFieldIgnoreRuleV0& rhs) {
    if (lhs.subsystem != rhs.subsystem) {
        return lhs.subsystem < rhs.subsystem;
    }
    if (lhs.type != rhs.type) {
        return lhs.type < rhs.type;
    }
    return lhs.field < rhs.field;
}

[[nodiscard]] astraea::core::Result<
    TraceDiffPolicyV0,
    TraceDiffError>
normalize_policy(TraceDiffPolicyV0 policy) {
    for (const auto& rule :
         policy.ignored_stable_fields) {
        if (!valid_ignore_rule(rule)) {
            return astraea::core::Result<
                TraceDiffPolicyV0,
                TraceDiffError>::failure(
                    diff_error(
                        TraceDiffErrorCode::
                            invalid_ignore_rule));
        }
    }

    std::sort(
        policy.ignored_stable_fields.begin(),
        policy.ignored_stable_fields.end(),
        ignore_rule_less);

    for (std::size_t index = 1;
         index <
         policy.ignored_stable_fields.size();
         ++index) {
        if (policy.ignored_stable_fields[
                index - 1U] ==
            policy.ignored_stable_fields[
                index]) {
            return astraea::core::Result<
                TraceDiffPolicyV0,
                TraceDiffError>::failure(
                    diff_error(
                        TraceDiffErrorCode::
                            duplicate_ignore_rule));
        }
    }

    return astraea::core::Result<
        TraceDiffPolicyV0,
        TraceDiffError>::success(
            std::move(policy));
}

[[nodiscard]] bool field_ignored(
    const TraceDiffPolicyV0& policy,
    const TraceEventV0& event,
    std::string_view field) noexcept {
    for (const auto& rule :
         policy.ignored_stable_fields) {
        if (rule.field != field) {
            continue;
        }
        if (rule.subsystem.has_value() &&
            rule.subsystem.value() !=
                event.subsystem) {
            continue;
        }
        if (rule.type.has_value() &&
            rule.type.value() != event.type) {
            continue;
        }
        return true;
    }

    return false;
}

[[nodiscard]] bool event_identity_equal(
    const TraceEventV0& lhs,
    const TraceEventV0& rhs) noexcept {
    return lhs.subsystem == rhs.subsystem &&
           lhs.type == rhs.type &&
           lhs.guest == rhs.guest;
}

[[nodiscard]] TraceEventIdentityV0
event_identity(const TraceEventV0& event) {
    return TraceEventIdentityV0{
        .subsystem = event.subsystem,
        .type = event.type,
        .guest = event.guest,
    };
}

[[nodiscard]] std::string event_path(
    std::size_t index,
    std::string_view suffix) {
    std::string path = "events[";
    path.append(std::to_string(index));
    path.append("]");
    if (!suffix.empty()) {
        path.push_back('.');
        path.append(suffix);
    }
    return path;
}

[[nodiscard]] std::optional<TraceValueV0>
optional_text_value(
    const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return TraceValueV0{value.value()};
}

[[nodiscard]] std::optional<TraceValueV0>
optional_u64_value(
    const std::optional<std::uint64_t>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return TraceValueV0{value.value()};
}

[[nodiscard]] TraceDiffResultV0
run_divergence(
    std::string path,
    std::optional<TraceValueV0> left,
    std::optional<TraceValueV0> right) {
    TraceDivergenceV0 divergence{};
    divergence.kind =
        TraceDivergenceKindV0::
            run_field_mismatch;
    divergence.path = std::move(path);
    divergence.left_value = std::move(left);
    divergence.right_value = std::move(right);

    return TraceDiffResultV0{
        .equivalent = false,
        .matched_event_count = 0,
        .first_divergence =
            std::move(divergence),
    };
}

[[nodiscard]] std::optional<TraceDiffResultV0>
compare_run_metadata(
    const TraceRunMetadataV0& left,
    const TraceRunMetadataV0& right) {
    if (left.target_kind != right.target_kind) {
        return run_divergence(
            "run.target_kind",
            TraceValueV0{left.target_kind},
            TraceValueV0{right.target_kind});
    }

    if (left.platform_family !=
        right.platform_family) {
        return run_divergence(
            "run.platform_family",
            TraceValueV0{left.platform_family},
            TraceValueV0{right.platform_family});
    }

    if (left.architecture != right.architecture) {
        return run_divergence(
            "run.architecture",
            TraceValueV0{left.architecture},
            TraceValueV0{right.architecture});
    }

    if (left.probe_id != right.probe_id) {
        return run_divergence(
            "run.probe.id",
            optional_text_value(left.probe_id),
            optional_text_value(right.probe_id));
    }

    if (left.probe_version !=
        right.probe_version) {
        return run_divergence(
            "run.probe.version",
            optional_u64_value(
                left.probe_version),
            optional_u64_value(
                right.probe_version));
    }

    if (left.case_sha256 != right.case_sha256) {
        return run_divergence(
            "run.case_sha256",
            optional_text_value(
                left.case_sha256),
            optional_text_value(
                right.case_sha256));
    }

    return std::nullopt;
}

[[nodiscard]] TraceDivergenceV0
event_base_divergence(
    TraceDivergenceKindV0 kind,
    std::string path,
    const TraceEventV0* left,
    std::optional<std::size_t> left_index,
    const TraceEventV0* right,
    std::optional<std::size_t> right_index) {
    TraceDivergenceV0 divergence{};
    divergence.kind = kind;
    divergence.path = std::move(path);
    divergence.left_event_index = left_index;
    divergence.right_event_index = right_index;

    if (left != nullptr) {
        divergence.left_event_id = left->id;
        divergence.left_identity =
            event_identity(*left);
    }

    if (right != nullptr) {
        divergence.right_event_id = right->id;
        divergence.right_identity =
            event_identity(*right);
    }

    return divergence;
}

[[nodiscard]] std::optional<TraceDivergenceV0>
compare_stable_fields(
    const TraceEventV0& left,
    std::size_t left_index,
    const TraceEventV0& right,
    std::size_t right_index,
    const TraceDiffPolicyV0& policy) {
    std::size_t left_field = 0;
    std::size_t right_field = 0;

    while (true) {
        while (left_field < left.stable.size() &&
               field_ignored(
                   policy,
                   left,
                   left.stable[left_field].name)) {
            ++left_field;
        }
        while (right_field < right.stable.size() &&
               field_ignored(
                   policy,
                   right,
                   right.stable[right_field].name)) {
            ++right_field;
        }

        if (left_field == left.stable.size() &&
            right_field == right.stable.size()) {
            return std::nullopt;
        }

        if (left_field == left.stable.size()) {
            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        stable_field_missing_left,
                    event_path(
                        right_index,
                        "stable." +
                            right.stable[
                                right_field].name),
                    &left,
                    left_index,
                    &right,
                    right_index);
            divergence.field_name =
                right.stable[right_field].name;
            divergence.right_value =
                right.stable[right_field].value;
            return divergence;
        }

        if (right_field == right.stable.size()) {
            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        stable_field_missing_right,
                    event_path(
                        left_index,
                        "stable." +
                            left.stable[
                                left_field].name),
                    &left,
                    left_index,
                    &right,
                    right_index);
            divergence.field_name =
                left.stable[left_field].name;
            divergence.left_value =
                left.stable[left_field].value;
            return divergence;
        }

        const auto& left_value =
            left.stable[left_field];
        const auto& right_value =
            right.stable[right_field];

        if (left_value.name < right_value.name) {
            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        stable_field_missing_right,
                    event_path(
                        left_index,
                        "stable." +
                            left_value.name),
                    &left,
                    left_index,
                    &right,
                    right_index);
            divergence.field_name =
                left_value.name;
            divergence.left_value =
                left_value.value;
            return divergence;
        }

        if (right_value.name < left_value.name) {
            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        stable_field_missing_left,
                    event_path(
                        right_index,
                        "stable." +
                            right_value.name),
                    &left,
                    left_index,
                    &right,
                    right_index);
            divergence.field_name =
                right_value.name;
            divergence.right_value =
                right_value.value;
            return divergence;
        }

        if (left_value.value != right_value.value) {
            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        stable_field_value_mismatch,
                    event_path(
                        left_index,
                        "stable." +
                            left_value.name),
                    &left,
                    left_index,
                    &right,
                    right_index);
            divergence.field_name =
                left_value.name;
            divergence.left_value =
                left_value.value;
            divergence.right_value =
                right_value.value;
            return divergence;
        }

        ++left_field;
        ++right_field;
    }
}

[[nodiscard]] std::size_t distance_to_identity(
    const std::vector<TraceEventV0>& events,
    std::size_t start,
    const TraceEventV0& target) noexcept {
    for (std::size_t index = start;
         index < events.size();
         ++index) {
        if (event_identity_equal(
                events[index],
                target)) {
            return index - start + 1U;
        }
    }

    return std::numeric_limits<std::size_t>::max();
}

void append_u64_hex(
    std::string& output,
    std::uint64_t value) {
    constexpr std::array<char, 16> kHex{
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'a', 'b',
        'c', 'd', 'e', 'f',
    };

    output.append("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        output.push_back(
            kHex[
                static_cast<std::size_t>(
                    (value >>
                     static_cast<unsigned int>(
                         shift)) &
                    0x0fU)]);
    }
}

void append_value(
    std::string& output,
    const std::optional<TraceValueV0>& value) {
    if (!value.has_value()) {
        output.append("<absent>");
        return;
    }

    if (const auto* integer =
            std::get_if<std::uint64_t>(
                &value.value());
        integer != nullptr) {
        append_u64_hex(output, *integer);
        return;
    }

    if (const auto* boolean =
            std::get_if<bool>(
                &value.value());
        boolean != nullptr) {
        output.append(
            *boolean ? "true" : "false");
        return;
    }

    if (const auto* text =
            std::get_if<std::string>(
                &value.value());
        text != nullptr) {
        output.push_back('"');
        for (const char character : *text) {
            if (character == '"' ||
                character == '\\') {
                output.push_back('\\');
            }
            output.push_back(character);
        }
        output.push_back('"');
        return;
    }

    const auto& bytes =
        std::get<std::vector<std::byte>>(
            value.value());
    constexpr std::array<char, 16> kHex{
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'a', 'b',
        'c', 'd', 'e', 'f',
    };
    output.append("hex:");
    for (const std::byte raw : bytes) {
        const auto byte =
            std::to_integer<unsigned int>(raw);
        output.push_back(
            kHex[(byte >> 4U) & 0x0fU]);
        output.push_back(
            kHex[byte & 0x0fU]);
    }
}

void append_identity(
    std::string& output,
    const std::optional<TraceEventIdentityV0>&
        identity) {
    if (!identity.has_value()) {
        output.append("<none>");
        return;
    }

    output.append(identity->subsystem);
    output.push_back('/');
    output.append(identity->type);
    if (identity->guest.has_value()) {
        output.append(" @ ");
        output.append(
            identity->guest->object);
        output.push_back('+');
        append_u64_hex(
            output,
            identity->guest->offset);
    }
}

}  // namespace

TraceDiffComputationResultV0 diff_trace_v0(
    TraceDocumentV0 left,
    TraceDocumentV0 right,
    TraceDiffPolicyV0 policy) {
    try {
        auto normalized_policy =
            normalize_policy(std::move(policy));
        if (!normalized_policy.has_value()) {
            return TraceDiffComputationResultV0::
                failure(
                    normalized_policy.error());
        }

        auto normalized_left =
            normalize_trace_v0(
                std::move(left));
        if (!normalized_left.has_value()) {
            return TraceDiffComputationResultV0::
                failure(
                    diff_error(
                        TraceDiffErrorCode::
                            invalid_left_trace,
                        normalized_left.error()));
        }

        auto normalized_right =
            normalize_trace_v0(
                std::move(right));
        if (!normalized_right.has_value()) {
            return TraceDiffComputationResultV0::
                failure(
                    diff_error(
                        TraceDiffErrorCode::
                            invalid_right_trace,
                        normalized_right.error()));
        }

        const auto& lhs = normalized_left.value();
        const auto& rhs = normalized_right.value();
        const auto& selected_policy =
            normalized_policy.value();

        if (auto run =
                compare_run_metadata(
                    lhs.run,
                    rhs.run);
            run.has_value()) {
            return TraceDiffComputationResultV0::
                success(
                    std::move(run.value()));
        }

        std::size_t left_index = 0;
        std::size_t right_index = 0;
        std::size_t matched = 0;

        while (left_index < lhs.events.size() &&
               right_index < rhs.events.size()) {
            const auto& left_event =
                lhs.events[left_index];
            const auto& right_event =
                rhs.events[right_index];

            if (event_identity_equal(
                    left_event,
                    right_event)) {
                if (selected_policy.compare_event_ids &&
                    left_event.id != right_event.id) {
                    auto divergence =
                        event_base_divergence(
                            TraceDivergenceKindV0::
                                event_id_mismatch,
                            event_path(
                                left_index,
                                "id"),
                            &left_event,
                            left_index,
                            &right_event,
                            right_index);
                    divergence.left_value =
                        TraceValueV0{
                            left_event.id};
                    divergence.right_value =
                        TraceValueV0{
                            right_event.id};

                    return TraceDiffComputationResultV0::
                        success(
                            TraceDiffResultV0{
                                .equivalent = false,
                                .matched_event_count =
                                    matched,
                                .first_divergence =
                                    std::move(
                                        divergence),
                            });
                }

                auto field_divergence =
                    compare_stable_fields(
                        left_event,
                        left_index,
                        right_event,
                        right_index,
                        selected_policy);
                if (field_divergence.has_value()) {
                    return TraceDiffComputationResultV0::
                        success(
                            TraceDiffResultV0{
                                .equivalent = false,
                                .matched_event_count =
                                    matched,
                                .first_divergence =
                                    std::move(
                                        field_divergence
                                            .value()),
                            });
                }

                ++left_index;
                ++right_index;
                ++matched;
                continue;
            }

            const std::size_t insertion_distance =
                distance_to_identity(
                    rhs.events,
                    right_index + 1U,
                    left_event);
            const std::size_t deletion_distance =
                distance_to_identity(
                    lhs.events,
                    left_index + 1U,
                    right_event);

            if (insertion_distance <
                deletion_distance) {
                auto divergence =
                    event_base_divergence(
                        TraceDivergenceKindV0::
                            event_insertion,
                        "events",
                        &left_event,
                        left_index,
                        &right_event,
                        right_index);
                divergence.event_count =
                    insertion_distance;

                return TraceDiffComputationResultV0::
                    success(
                        TraceDiffResultV0{
                            .equivalent = false,
                            .matched_event_count =
                                matched,
                            .first_divergence =
                                std::move(
                                    divergence),
                        });
            }

            if (deletion_distance <
                insertion_distance) {
                auto divergence =
                    event_base_divergence(
                        TraceDivergenceKindV0::
                            event_deletion,
                        "events",
                        &left_event,
                        left_index,
                        &right_event,
                        right_index);
                divergence.event_count =
                    deletion_distance;

                return TraceDiffComputationResultV0::
                    success(
                        TraceDiffResultV0{
                            .equivalent = false,
                            .matched_event_count =
                                matched,
                            .first_divergence =
                                std::move(
                                    divergence),
                        });
            }

            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        event_identity_mismatch,
                    "events",
                    &left_event,
                    left_index,
                    &right_event,
                    right_index);

            return TraceDiffComputationResultV0::
                success(
                    TraceDiffResultV0{
                        .equivalent = false,
                        .matched_event_count =
                            matched,
                        .first_divergence =
                            std::move(divergence),
                    });
        }

        if (left_index < lhs.events.size()) {
            const auto& event =
                lhs.events[left_index];
            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        event_deletion,
                    "events",
                    &event,
                    left_index,
                    nullptr,
                    std::nullopt);
            divergence.event_count =
                lhs.events.size() - left_index;

            return TraceDiffComputationResultV0::
                success(
                    TraceDiffResultV0{
                        .equivalent = false,
                        .matched_event_count =
                            matched,
                        .first_divergence =
                            std::move(divergence),
                    });
        }

        if (right_index < rhs.events.size()) {
            const auto& event =
                rhs.events[right_index];
            auto divergence =
                event_base_divergence(
                    TraceDivergenceKindV0::
                        event_insertion,
                    "events",
                    nullptr,
                    std::nullopt,
                    &event,
                    right_index);
            divergence.event_count =
                rhs.events.size() - right_index;

            return TraceDiffComputationResultV0::
                success(
                    TraceDiffResultV0{
                        .equivalent = false,
                        .matched_event_count =
                            matched,
                        .first_divergence =
                            std::move(divergence),
                    });
        }

        return TraceDiffComputationResultV0::success(
            TraceDiffResultV0{
                .equivalent = true,
                .matched_event_count = matched,
                .first_divergence = std::nullopt,
            });
    } catch (const std::bad_alloc&) {
        return TraceDiffComputationResultV0::failure(
            diff_error(
                TraceDiffErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return TraceDiffComputationResultV0::failure(
            diff_error(
                TraceDiffErrorCode::
                    host_allocation_failure));
    }
}

TraceDiffReportResultV0 format_trace_diff_v0(
    const TraceDiffResultV0& result) {
    try {
        if (result.equivalent) {
            return TraceDiffReportResultV0::success(
                "traces are behaviorally equivalent under the selected policy");
        }

        if (!result.first_divergence.has_value()) {
            return TraceDiffReportResultV0::success(
                "traces differ, but no first divergence was recorded");
        }

        const auto& divergence =
            result.first_divergence.value();
        std::string report = "first divergence: ";

        switch (divergence.kind) {
        case TraceDivergenceKindV0::
            run_field_mismatch:
            report.append(divergence.path);
            report.append(" differs (left=");
            append_value(
                report,
                divergence.left_value);
            report.append(", right=");
            append_value(
                report,
                divergence.right_value);
            report.push_back(')');
            break;

        case TraceDivergenceKindV0::
            event_id_mismatch:
            report.append(divergence.path);
            report.append(" differs (left=");
            append_value(
                report,
                divergence.left_value);
            report.append(", right=");
            append_value(
                report,
                divergence.right_value);
            report.push_back(')');
            break;

        case TraceDivergenceKindV0::
            event_insertion:
            report.append(
                std::to_string(
                    divergence.event_count));
            report.append(
                divergence.event_count == 1U
                    ? " event inserted on right"
                    : " events inserted on right");
            if (divergence.right_identity.has_value()) {
                report.append(": ");
                append_identity(
                    report,
                    divergence.right_identity);
            }
            break;

        case TraceDivergenceKindV0::
            event_deletion:
            report.append(
                std::to_string(
                    divergence.event_count));
            report.append(
                divergence.event_count == 1U
                    ? " event deleted from right"
                    : " events deleted from right");
            if (divergence.left_identity.has_value()) {
                report.append(": ");
                append_identity(
                    report,
                    divergence.left_identity);
            }
            break;

        case TraceDivergenceKindV0::
            event_identity_mismatch:
            report.append(
                "event identity differs (left=");
            append_identity(
                report,
                divergence.left_identity);
            report.append(", right=");
            append_identity(
                report,
                divergence.right_identity);
            report.push_back(')');
            break;

        case TraceDivergenceKindV0::
            stable_field_missing_left:
            report.append(divergence.path);
            report.append(
                " is absent on left; right=");
            append_value(
                report,
                divergence.right_value);
            break;

        case TraceDivergenceKindV0::
            stable_field_missing_right:
            report.append(divergence.path);
            report.append(
                " is absent on right; left=");
            append_value(
                report,
                divergence.left_value);
            break;

        case TraceDivergenceKindV0::
            stable_field_value_mismatch:
            report.append(divergence.path);
            report.append(" differs (left=");
            append_value(
                report,
                divergence.left_value);
            report.append(", right=");
            append_value(
                report,
                divergence.right_value);
            report.push_back(')');
            break;
        }

        return TraceDiffReportResultV0::success(
            std::move(report));
    } catch (const std::bad_alloc&) {
        return TraceDiffReportResultV0::failure(
            diff_error(
                TraceDiffErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return TraceDiffReportResultV0::failure(
            diff_error(
                TraceDiffErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::trace
