#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/trace/v0.hpp>

namespace astraea::trace {

enum class TraceDiffErrorCode {
    invalid_left_trace,
    invalid_right_trace,
    invalid_ignore_rule,
    duplicate_ignore_rule,
    host_allocation_failure,
};

struct TraceDiffError {
    TraceDiffErrorCode code =
        TraceDiffErrorCode::host_allocation_failure;
    bool has_trace_error = false;
    TraceError trace_error;

    auto operator<=>(const TraceDiffError&) const = default;
};

struct TraceStableFieldIgnoreRuleV0 {
    std::optional<std::string> subsystem;
    std::optional<std::string> type;
    std::string field;

    auto operator<=>(const TraceStableFieldIgnoreRuleV0&) const = default;
};

struct TraceDiffPolicyV0 {
    bool compare_event_ids = true;
    std::vector<TraceStableFieldIgnoreRuleV0>
        ignored_stable_fields;

    auto operator<=>(const TraceDiffPolicyV0&) const = default;
};

struct TraceEventIdentityV0 {
    std::string subsystem;
    std::string type;
    std::optional<GuestLocationV0> guest;

    auto operator<=>(const TraceEventIdentityV0&) const = default;
};

enum class TraceDivergenceKindV0 {
    run_field_mismatch,
    event_id_mismatch,
    event_insertion,
    event_deletion,
    event_identity_mismatch,
    stable_field_missing_left,
    stable_field_missing_right,
    stable_field_value_mismatch,
};

struct TraceDivergenceV0 {
    TraceDivergenceKindV0 kind =
        TraceDivergenceKindV0::run_field_mismatch;
    std::string path;

    std::optional<std::size_t> left_event_index;
    std::optional<std::size_t> right_event_index;
    std::optional<std::uint64_t> left_event_id;
    std::optional<std::uint64_t> right_event_id;

    std::optional<TraceEventIdentityV0> left_identity;
    std::optional<TraceEventIdentityV0> right_identity;

    std::optional<std::string> field_name;
    std::optional<TraceValueV0> left_value;
    std::optional<TraceValueV0> right_value;

    std::size_t event_count = 0;

    auto operator<=>(const TraceDivergenceV0&) const = default;
};

struct TraceDiffResultV0 {
    bool equivalent = true;
    std::size_t matched_event_count = 0;
    std::optional<TraceDivergenceV0> first_divergence;

    auto operator<=>(const TraceDiffResultV0&) const = default;
};

using TraceDiffComputationResultV0 =
    astraea::core::Result<
        TraceDiffResultV0,
        TraceDiffError>;

using TraceDiffReportResultV0 =
    astraea::core::Result<
        std::string,
        TraceDiffError>;

[[nodiscard]] TraceDiffComputationResultV0
diff_trace_v0(
    TraceDocumentV0 left,
    TraceDocumentV0 right,
    TraceDiffPolicyV0 policy = {});

[[nodiscard]] TraceDiffReportResultV0
format_trace_diff_v0(
    const TraceDiffResultV0& result);

}  // namespace astraea::trace
